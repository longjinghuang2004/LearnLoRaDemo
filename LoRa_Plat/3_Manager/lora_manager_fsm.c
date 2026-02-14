/**
  ******************************************************************************
  * @file    lora_manager_fsm.c
  * @author  LoRaPlat Team
  * @brief   LoRa 协议状态机实现 (V3.6 可靠传输版)
  *          集成自动重传 (Retransmission) 与接收去重 (De-duplication)
  ******************************************************************************
  */

#include "lora_manager_fsm.h"
#include "lora_manager_buffer.h"
#include "lora_port.h"
#include "lora_osal.h"
#include "lora_service.h" 
#include <string.h>

// ============================================================
//                    1. 内部数据结构
// ============================================================

// 去重表条目
typedef struct {
    uint16_t src_id;
    uint8_t  seq;
    uint32_t last_seen; // 时间戳 (用于 LRU)
    bool     valid;
} DeDupEntry_t;

typedef struct {
    LoRa_FSM_State_t state;
    uint32_t         timer_start;
    uint8_t          retry_count;
    uint8_t          tx_seq; // 本机当前的发送序号
    
    // --- 重传上下文 ---
    // 备份已发送但未确认的数据包 (序列化后的物理层数据)
    // 大小 = MaxPayload + ProtocolOverhead (~15B) + Margin
    uint8_t  pending_buf[LORA_MAX_PAYLOAD_LEN + 32];
    uint16_t pending_len;
    
    // --- ACK 发送上下文 ---
    struct {
        bool     pending;
        uint16_t target_id;
        uint8_t  seq;
    } ack_ctx;
    
    // --- 接收去重表 ---
    DeDupEntry_t dedup_table[LORA_DEDUP_MAX_COUNT];
    
} FSM_Context_t;

static FSM_Context_t s_FSM;

// ============================================================
//                    2. 内部辅助函数
// ============================================================

static void _FSM_Reset(void) {
    s_FSM.state = LORA_FSM_IDLE;
    s_FSM.ack_ctx.pending = false;
    s_FSM.retry_count = 0;
    s_FSM.pending_len = 0; // 清空备份
}

// 发送 ACK (使用局部栈变量)
static void _FSM_SendAck(void) {
    LoRa_Packet_t pkt;
    uint8_t ack_stack_buf[64]; 
    
    memset(&pkt, 0, sizeof(pkt));
    pkt.IsAckPacket = true;
    pkt.NeedAck = false; 
    pkt.HasCrc = LORA_ENABLE_CRC;
    pkt.TargetID = s_FSM.ack_ctx.target_id;
    pkt.SourceID = LoRa_Service_GetConfig()->net_id;
    pkt.Sequence = s_FSM.ack_ctx.seq; 
    pkt.PayloadLen = 0;
    
    const LoRa_Config_t *cfg = LoRa_Service_GetConfig();
    LoRa_Manager_Buffer_PushAck(&pkt, cfg->tmode, cfg->channel, ack_stack_buf, sizeof(ack_stack_buf));
    
    s_FSM.ack_ctx.pending = false;
}

// 检查包是否重复 (LRU 策略)
// 返回 true 表示是重复包，应丢弃
static bool _FSM_CheckDuplicate(uint16_t src_id, uint8_t seq) {
    uint32_t now = OSAL_GetTick();
    int lru_idx = 0;
    uint32_t min_time = 0xFFFFFFFF;
    
    // 1. 遍历查找
    for (int i = 0; i < LORA_DEDUP_MAX_COUNT; i++) {
        if (s_FSM.dedup_table[i].valid) {
            // 找到匹配
            if (s_FSM.dedup_table[i].src_id == src_id) {
                if (s_FSM.dedup_table[i].seq == seq) {
                    s_FSM.dedup_table[i].last_seen = now; // 更新活跃时间
                    return true; // 重复！
                } else {
                    // 同一个设备的新包，更新 Seq 和时间
                    s_FSM.dedup_table[i].seq = seq;
                    s_FSM.dedup_table[i].last_seen = now;
                    return false; // 新包
                }
            }
            
            // 寻找最久未使用的条目 (LRU)
            if (s_FSM.dedup_table[i].last_seen < min_time) {
                min_time = s_FSM.dedup_table[i].last_seen;
                lru_idx = i;
            }
        } else {
            // 发现空槽，直接使用
            lru_idx = i;
            min_time = 0; // 标记为最高优先级覆盖
            break; // 只要有空槽，就不需要继续找 LRU 了，除非为了完全遍历
            // 这里简化：遇到空槽直接用，不继续找了
        }
    }
    
    // 2. 未找到匹配，插入新条目 (覆盖 LRU 或 空槽)
    s_FSM.dedup_table[lru_idx].valid = true;
    s_FSM.dedup_table[lru_idx].src_id = src_id;
    s_FSM.dedup_table[lru_idx].seq = seq;
    s_FSM.dedup_table[lru_idx].last_seen = now;
    
    return false; // 新包
}

// ============================================================
//                    3. 核心接口实现
// ============================================================

void LoRa_Manager_FSM_Init(void) {
    _FSM_Reset();
    s_FSM.tx_seq = 0;
    memset(s_FSM.dedup_table, 0, sizeof(s_FSM.dedup_table));
}

bool LoRa_Manager_FSM_Send(const uint8_t *payload, uint16_t len, uint16_t target_id,
                           uint8_t *scratch_buf, uint16_t scratch_len) {
    
    if (s_FSM.state != LORA_FSM_IDLE) {
        LORA_LOG("[MGR] Send Reject: Busy\r\n");
        return false; 
    }

    LoRa_Packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    
    pkt.IsAckPacket = false;
    pkt.NeedAck = (LORA_ENABLE_ACK && target_id != 0xFFFF); 
    pkt.HasCrc = LORA_ENABLE_CRC;
    pkt.TargetID = target_id;
    pkt.SourceID = LoRa_Service_GetConfig()->net_id;
    pkt.Sequence = ++s_FSM.tx_seq;
    pkt.PayloadLen = len;
    if (len > LORA_MAX_PAYLOAD_LEN) len = LORA_MAX_PAYLOAD_LEN;
    memcpy(pkt.Payload, payload, len);
    
    const LoRa_Config_t *cfg = LoRa_Service_GetConfig();
    
    // 【关键变更 1】先序列化到 FSM 的备份缓冲区 (Pending Buffer)
    // 我们直接利用 Protocol_Pack 将数据打包到 pending_buf 中
    // 注意：这里我们“借用”了 pending_buf 作为 scratch_buf 的一部分功能
    // 但为了接口兼容，我们还是先打包到 scratch_buf，再拷贝到 pending_buf？
    // 不，直接打包到 pending_buf 更高效。
    
    uint16_t packed_len = LoRa_Manager_Protocol_Pack(&pkt, s_FSM.pending_buf, sizeof(s_FSM.pending_buf), cfg->tmode, cfg->channel);
    
    if (packed_len == 0) return false;
    
    s_FSM.pending_len = packed_len;
    
    // 【关键变更 2】将备份缓冲区的数据推入发送队列
    // 这里我们需要一个“Raw Push”接口，或者复用 PushTx 但绕过 Pack 过程？
    // 现有的 PushTx 接口接受 Packet 结构体并内部 Pack。这会导致两次 Pack。
    // 为了复用且不修改 Buffer 接口，我们这里还是走标准 PushTx。
    // 等等，如果走标准 PushTx，那 pending_buf 存的是什么？
    // 方案 A: pending_buf 存 Packet 结构体 -> 太大。
    // 方案 B: pending_buf 存序列化后的数据 -> 需要 Buffer 层提供 PushRaw 接口。
    // 方案 C: 既然 Send 已经传入了 scratch_buf，我们先用 scratch_buf 序列化，然后 memcpy 到 pending_buf。
    //        然后调用 PushTx (它内部会再次序列化，有点浪费)。
    
    // 修正方案：为了不修改 Buffer 接口太复杂，我们采取“重传时重新 Pack”的策略？
    // 不行，重传必须保证 Seq 不变。如果重新 Pack，Packet 结构体从哪来？
    // 所以必须备份“序列化后的数据”。
    
    // 让我们修改 Buffer 接口？不，太麻烦。
    // 我们直接操作 RingBuffer？不，破坏封装。
    // 最佳路径：在 Buffer 层增加 `LoRa_Manager_Buffer_PushRaw` 接口。
    // 但为了 Phase 2 Iteration 1 的快速落地，我们暂时用一个变通方法：
    // 我们已经有了 packed 数据在 pending_buf 里。
    // 我们手动调用 RingBuffer_Write (需要 include ring_buffer.h 且能访问 s_TxRing)。
    // 这破坏了封装。
    
    // 回退一步：我们在 FSM 里备份 Packet 结构体？
    // Packet 结构体包含 Payload (200B)，加上其他字段，约 210B。
    // 序列化后的数据也是 200B+。两者大小差不多。
    // 备份结构体更简单！重传时直接把结构体传给 PushTx 即可！
    
    // 【最终决策】备份 Packet 结构体 (Payload 部分)。
    // 为了节省 RAM，我们只备份 Payload 和关键头信息。
    // 但为了代码简单，我们直接备份整个 Packet 结构体。
    // FSM_Context_t 增加 LoRa_Packet_t pending_pkt;
    
    // (由于我无法修改上面的 typedef，这里假设 pending_buf 足够大，我们把 packet memcpy 进去)
    // 实际上 pending_buf 是 uint8_t 数组。我们可以强制转型。
    
    if (sizeof(LoRa_Packet_t) > sizeof(s_FSM.pending_buf)) {
        LORA_LOG("[MGR] Err: Pending Buf too small\r\n");
        return false;
    }
    
    memcpy(s_FSM.pending_buf, &pkt, sizeof(LoRa_Packet_t));
    s_FSM.pending_len = 1; // 标记为有效 (非0即可)
    
    // 推入队列
    return LoRa_Manager_Buffer_PushTx(&pkt, cfg->tmode, cfg->channel, scratch_buf, scratch_len);
}

// 【修改】返回值改为 bool
bool LoRa_Manager_FSM_ProcessRxPacket(const LoRa_Packet_t *packet) {
    // 1. 如果是 ACK 包
    if (packet->IsAckPacket) {
        if (s_FSM.state == LORA_FSM_WAIT_ACK) {
            // 校验 Seq (简单匹配)
            // 从 pending_pkt 中获取刚才发的 Seq
            LoRa_Packet_t *pending = (LoRa_Packet_t*)s_FSM.pending_buf;
            
            if (packet->Sequence == pending->Sequence) {
                LORA_LOG("[MGR] ACK Recv (Seq %d)\r\n", packet->Sequence);
                _FSM_Reset(); // 成功，清除备份
                // TODO: Event Callback Success
            } else {
                LORA_LOG("[MGR] ACK Seq Mismatch: Exp %d, Got %d\r\n", pending->Sequence, packet->Sequence);
            }
        }
        return false; // ACK 包不回调上层
    } 
    // 2. 如果是数据包
    else {
        // 【关键变更 3】去重检查
        if (_FSM_CheckDuplicate(packet->SourceID, packet->Sequence)) {
            LORA_LOG("[MGR] Drop Duplicate (ID %x, Seq %d)\r\n", packet->SourceID, packet->Sequence);
            
            // 即使是重复包，如果对方要求 ACK，我们也必须回 ACK
            // 因为对方重传说明它没收到我们之前的 ACK
            if (packet->NeedAck && packet->TargetID != 0xFFFF) {
                s_FSM.ack_ctx.target_id = packet->SourceID;
                s_FSM.ack_ctx.seq = packet->Sequence;
                s_FSM.ack_ctx.pending = true;
                s_FSM.state = LORA_FSM_ACK_DELAY;
                s_FSM.timer_start = OSAL_GetTick();
            }
            return false; // 重复包，不回调
        }
        
        // 有效新包
        if (packet->NeedAck && packet->TargetID != 0xFFFF) {
            s_FSM.ack_ctx.target_id = packet->SourceID;
            s_FSM.ack_ctx.seq = packet->Sequence;
            s_FSM.ack_ctx.pending = true;
            s_FSM.state = LORA_FSM_ACK_DELAY;
            s_FSM.timer_start = OSAL_GetTick();
        }
        return true; // 需回调
    }
}

void LoRa_Manager_FSM_Run(uint8_t *scratch_buf, uint16_t scratch_len) {
    uint32_t now = OSAL_GetTick();
    
    // 1. 物理层发送调度 (保持不变)
    if (s_FSM.state == LORA_FSM_IDLE && LoRa_Manager_Buffer_HasTxData()) {
        if (!LoRa_Port_IsTxBusy()) {
            uint16_t len = LoRa_Manager_Buffer_PeekTx(scratch_buf, scratch_len);
            if (len > 0) {
                LORA_HEXDUMP("TX RAW", scratch_buf, len);
                if (LoRa_Port_TransmitData(scratch_buf, len) > 0) {
                    LoRa_Manager_Buffer_PopTx(len);
                    LoRa_Service_NotifyEvent(LORA_EVENT_MSG_SENT, NULL);
                    
                    // 检查是否需要等待 ACK
                    const LoRa_Config_t *cfg = LoRa_Service_GetConfig();
                    uint8_t offset = (cfg->tmode == 1) ? 3 : 0;
                    if (len > offset + 3) {
                        if (scratch_buf[offset + 3] & LORA_CTRL_MASK_NEED_ACK) {
                            s_FSM.state = LORA_FSM_WAIT_ACK;
                            s_FSM.timer_start = now;
                            s_FSM.retry_count = 0;
                            LORA_LOG("[MGR] Wait ACK...\r\n");
                        }
                    }
                }
            }
        }
    }
    
    // 2. 协议层状态机
    switch (s_FSM.state) {
        case LORA_FSM_IDLE:
            break;
            
        case LORA_FSM_ACK_DELAY:
            if (now - s_FSM.timer_start > LORA_ACK_DELAY_MS) {
                if (s_FSM.ack_ctx.pending) {
                    _FSM_SendAck();
                    LORA_LOG("[MGR] ACK Queued\r\n");
                }
                s_FSM.state = LORA_FSM_IDLE;
            }
            break;
            
        case LORA_FSM_WAIT_ACK:
            if (now - s_FSM.timer_start > LORA_RETRY_INTERVAL_MS) {
                if (s_FSM.retry_count < LORA_MAX_RETRY) {
                    s_FSM.retry_count++;
                    LORA_LOG("[MGR] ACK Timeout, Retry %d\r\n", s_FSM.retry_count);
                    
                    // 【关键变更 4】执行重传
                    if (s_FSM.pending_len > 0) {
                        LoRa_Packet_t *pending = (LoRa_Packet_t*)s_FSM.pending_buf;
                        const LoRa_Config_t *cfg = LoRa_Service_GetConfig();
                        
                        // 重新推入队列
                        // 注意：这里使用 scratch_buf 进行序列化，这是安全的，因为 Run 拥有 scratch_buf
                        LoRa_Manager_Buffer_PushTx(pending, cfg->tmode, cfg->channel, scratch_buf, scratch_len);
                        
                        // 重置计时器
                        s_FSM.timer_start = now;
                    } else {
                        // 异常：没有备份数据？
                        _FSM_Reset();
                    }
                } else {
                    LORA_LOG("[MGR] ACK Failed (Max Retry)\r\n");
                    _FSM_Reset();
                }
            }
            break;
            
        default:
            _FSM_Reset();
            break;
    }
}

bool LoRa_Manager_FSM_IsBusy(void) {
    return s_FSM.state != LORA_FSM_IDLE;
}
