/**
  ******************************************************************************
  * @file    lora_manager_fsm.c
  * @author  LoRaPlat Team
  * @brief   LoRa 协议状态机实现 (V3.4.1 Fixes)
  ******************************************************************************
  */

#include "LoRaPlatConfig.h"
#include "lora_manager_fsm.h"
#include "lora_manager_buffer.h"
#include "lora_port.h"
#include "lora_osal.h"
#include <string.h>


//#define LORA_DEDUP_TTL_MS  5000  // 去重记录有效期 (5秒)，已移动至LoRaPlatConfig.h进行管理

// ============================================================
//                    1. 内部数据结构
// ============================================================

static const LoRa_Config_t *s_FSM_Config = NULL;


// 去重表条目
typedef struct {
    uint16_t src_id;
    uint16_t  seq;
    uint32_t last_seen; 
    bool     valid;
} DeDupEntry_t;

typedef struct {
    LoRa_FSM_State_t state;
    uint32_t         timeout_deadline; 
    uint8_t          retry_count;
    uint8_t          tx_seq; 
    
    // 当前正在处理的消息 ID
    LoRa_MsgID_t     current_tx_id;
    
    // --- 重传/广播上下文 ---
    uint8_t  pending_buf[LORA_MAX_PAYLOAD_LEN + 32];
    uint16_t pending_len;
    
    // --- ACK 发送上下文 ---
    struct {
        bool     pending;
        uint16_t target_id;
        uint16_t  seq;
    } ack_ctx;
    
    // --- 接收去重表 ---
    DeDupEntry_t dedup_table[LORA_DEDUP_MAX_COUNT];
    
} FSM_Context_t;

static FSM_Context_t s_FSM;

// 内部事件队列 (用于跨函数传递事件)
static LoRa_FSM_Output_t s_PendingOutput = { .Event = FSM_EVT_NONE, .MsgID = 0 };

// ============================================================
//                    2. 内部辅助函数 (Actions)
// ============================================================

// 辅助：设置待处理事件
static void _SetPendingEvent(LoRa_FSM_EventType_t evt, LoRa_MsgID_t id) {
    s_PendingOutput.Event = evt;
    s_PendingOutput.MsgID = id;
}

static void _FSM_SetState(LoRa_FSM_State_t new_state, uint32_t timeout_ms) {
    s_FSM.state = new_state;
    if (timeout_ms == LORA_TIMEOUT_INFINITE) {
        s_FSM.timeout_deadline = LORA_TIMEOUT_INFINITE;
    } else {
        s_FSM.timeout_deadline = OSAL_GetTick() + timeout_ms;
    }
}

static void _FSM_Reset(void) {
    _FSM_SetState(LORA_FSM_IDLE, LORA_TIMEOUT_INFINITE);
    s_FSM.ack_ctx.pending = false;
    s_FSM.retry_count = 0;
    s_FSM.pending_len = 0; 
    s_FSM.current_tx_id = 0; 
}

static void _FSM_SendAck(void) {
    LoRa_Packet_t pkt;
    uint8_t ack_stack_buf[64]; 
    
    memset(&pkt, 0, sizeof(pkt));
    pkt.IsAckPacket = true;
    pkt.NeedAck = false; 
    pkt.HasCrc = LORA_ENABLE_CRC;
    pkt.TargetID = s_FSM.ack_ctx.target_id;
    pkt.SourceID = s_FSM_Config->net_id;
    pkt.Sequence = s_FSM.ack_ctx.seq; 
    pkt.PayloadLen = 0;
    
    LoRa_Manager_Buffer_PushAck(&pkt, s_FSM_Config->tmode, s_FSM_Config->channel, ack_stack_buf, sizeof(ack_stack_buf));
    
    s_FSM.ack_ctx.pending = false;
}

/**
 * @brief 内部静态去重检查 (带 TTL 机制)
 * @param src_id 源设备 ID
 * @param seq    数据包序号
 * @return true=是重复包(丢弃), false=是新包(接受)
 */
static bool _FSM_CheckDuplicate(uint16_t src_id, uint16_t seq) {
    uint32_t now = OSAL_GetTick();
    int lru_idx = 0;
    uint32_t min_time = 0xFFFFFFFF;
    bool found_empty_slot = false;
    
    for (int i = 0; i < LORA_DEDUP_MAX_COUNT; i++) {
        // 1. 检查条目有效性与 TTL
        if (s_FSM.dedup_table[i].valid) {
            // 计算时间差 (处理溢出)
            uint32_t elapsed = now - s_FSM.dedup_table[i].last_seen;
            
            if (elapsed > LORA_DEDUP_TTL_MS) {
                // 条目超时，标记失效
                s_FSM.dedup_table[i].valid = false;
                // 记录为空槽位 (如果还没找到其他空槽)
                if (!found_empty_slot) {
                    lru_idx = i;
                    found_empty_slot = true;
                }
                continue; 
            }

            // 2. 匹配 SrcID
            if (s_FSM.dedup_table[i].src_id == src_id) {
                // 匹配 Seq
                if (s_FSM.dedup_table[i].seq == seq) {
                    // 完全匹配 -> 重复包
                    s_FSM.dedup_table[i].last_seen = now; // 刷新时间
                    return true; 
                } else {
                    // 同源新 Seq -> 更新记录
                    s_FSM.dedup_table[i].seq = seq;
                    s_FSM.dedup_table[i].last_seen = now;
                    return false; // 新包
                }
            }
            
            // 3. 寻找 LRU (最久未使用的有效条目)
            if (!found_empty_slot && s_FSM.dedup_table[i].last_seen < min_time) {
                min_time = s_FSM.dedup_table[i].last_seen;
                lru_idx = i;
            }
        } else {
            // 找到空槽
            if (!found_empty_slot) {
                lru_idx = i;
                found_empty_slot = true;
            }
        }
    }
    
    // 4. 插入新记录 (覆盖 LRU 或 填充空槽)
    s_FSM.dedup_table[lru_idx].valid = true;
    s_FSM.dedup_table[lru_idx].src_id = src_id;
    s_FSM.dedup_table[lru_idx].seq = seq;
    s_FSM.dedup_table[lru_idx].last_seen = now;
    
    return false; // 新包
}



// ============================================================
//                    3. 状态处理函数 (State Handlers)
// ============================================================

static bool _FSM_Action_PhyTxScheduler(uint8_t *scratch_buf, uint16_t scratch_len, bool *is_need_ack) {
    if (LoRa_Port_IsTxBusy()) return false;
    
    // 优先处理 ACK 队列
    if (LoRa_Manager_Buffer_HasAckData()) {
        uint16_t len = LoRa_Manager_Buffer_PeekAck(scratch_buf, scratch_len);
        if (len > 0) {
            if (LoRa_Port_TransmitData(scratch_buf, len) > 0) {
                LoRa_Manager_Buffer_PopAck(len);
                if (is_need_ack) *is_need_ack = false; 
                return true;
            }
        }
    }
    // 处理普通数据队列
    else if (s_FSM.state == LORA_FSM_IDLE && LoRa_Manager_Buffer_HasTxData()) {
        uint16_t len = LoRa_Manager_Buffer_PeekTx(scratch_buf, scratch_len);
        if (len > 0) {
            if (LoRa_Port_TransmitData(scratch_buf, len) > 0) {
                LoRa_Manager_Buffer_PopTx(len);
                
                if (is_need_ack) {
                    uint8_t offset = (s_FSM_Config->tmode == 1) ? 3 : 0;
                    if (len > offset + 3) {
                        *is_need_ack = (scratch_buf[offset + 3] & LORA_CTRL_MASK_NEED_ACK);
                    } else {
                        *is_need_ack = false;
                    }
                }
                return true;
            }
        }
    }
    return false;
}


/**
 * @brief 处理 ACK 等待超时逻辑 (重传策略核心)
 * @return true=状态已变更(重传或失败), false=无动作(物理层忙)
 */
static bool _FSM_HandleAckTimeout(uint8_t *scratch_buf, uint16_t scratch_len, LoRa_FSM_Output_t *output) {
    // 1. 检查重传次数是否耗尽
    if (s_FSM.retry_count >= LORA_MAX_RETRY) {
        LORA_LOG("[MGR] ACK Failed (Max Retry)\r\n");
        output->Event = FSM_EVT_TX_TIMEOUT;
        output->MsgID = s_FSM.current_tx_id;
        _FSM_Reset();
        return true;
    }

    // 2. 执行重传准备
    s_FSM.retry_count++;

    // [策略] 线性退避 + 随机抖动
    // Base: 1500ms, Step: 500ms, Jitter: 0~500ms
    // Retry 1: 2000~2500ms
    // Retry 2: 2500~3000ms
    // Retry 3: 3000~3500ms
    uint32_t step_add = s_FSM.retry_count * 500;
    
    // [关键] 这里暂时调用 Port 层，稍后讨论架构问题
    uint32_t jitter = LoRa_Port_GetEntropy32() % 501; 
    
    uint32_t next_timeout = LORA_RETRY_INTERVAL_MS + step_add + jitter;

    LORA_LOG("[MGR] ACK Timeout, Retry %d/%d (Next: %dms)\r\n", 
             s_FSM.retry_count, LORA_MAX_RETRY, next_timeout);

    // 3. 重新入队
    if (s_FSM.pending_len > 0) {
        LoRa_Packet_t *pending = (LoRa_Packet_t*)s_FSM.pending_buf;
        LoRa_Manager_Buffer_PushTx(pending, s_FSM_Config->tmode, s_FSM_Config->channel, scratch_buf, scratch_len);
        
        // 4. 尝试立即调度
        // 临时切换状态以允许调度器工作
        LoRa_FSM_State_t backup_state = s_FSM.state;
        s_FSM.state = LORA_FSM_IDLE;
        
        if (_FSM_Action_PhyTxScheduler(scratch_buf, scratch_len, NULL)) {
            // 发送成功，设置下一次超时
            _FSM_SetState(LORA_FSM_WAIT_ACK, next_timeout);
            return true;
        } else {
            // 物理层忙，恢复状态，下个循环再试
            s_FSM.state = backup_state; 
            return false;
        }
    } else {
        // 异常：Pending Buffer 为空
        _FSM_Reset();
        return true;
    }
}

// ============================================================
//                    4. 核心接口实现
// ============================================================

void LoRa_Manager_FSM_Init(const LoRa_Config_t *cfg) {
    LORA_CHECK_VOID(cfg);
    s_FSM_Config = cfg; 
    _FSM_Reset();
    s_FSM.tx_seq = 0;
    memset(s_FSM.dedup_table, 0, sizeof(s_FSM.dedup_table));
    s_PendingOutput.Event = FSM_EVT_NONE;
}

uint32_t LoRa_Manager_FSM_GetNextTimeout(void) {
    if (s_FSM.timeout_deadline == LORA_TIMEOUT_INFINITE) {
        return LORA_TIMEOUT_INFINITE;
    }
    uint32_t now = OSAL_GetTick();
    if ((int32_t)(s_FSM.timeout_deadline - now) <= 0) {
        return 0; 
    } else {
        return s_FSM.timeout_deadline - now; 
    }
}

// [修复] 实现了 IsBusy
bool LoRa_Manager_FSM_IsBusy(void) {
    // 只要状态不是 IDLE，或者有挂起的事件未处理，都视为忙
    return (s_FSM.state != LORA_FSM_IDLE) || (s_PendingOutput.Event != FSM_EVT_NONE);
}

bool LoRa_Manager_FSM_Send(const uint8_t *payload, uint16_t len, uint16_t target_id, LoRa_SendOpt_t opt,
                           LoRa_MsgID_t msg_id,
                           uint8_t *scratch_buf, uint16_t scratch_len) {
    
    if (s_FSM.state != LORA_FSM_IDLE) {
        LORA_LOG("[MGR] Send Reject: Busy\r\n");
        return false; 
    }

    static LoRa_Packet_t s_TempPkt; 
    memset(&s_TempPkt, 0, sizeof(s_TempPkt));
    
    s_TempPkt.IsAckPacket = false;
    s_TempPkt.NeedAck = (target_id == 0xFFFF) ? false : opt.NeedAck;
    s_TempPkt.HasCrc = LORA_ENABLE_CRC;
    s_TempPkt.TargetID = target_id;
    s_TempPkt.SourceID = s_FSM_Config->net_id;
    s_TempPkt.Sequence = ++s_FSM.tx_seq;
    s_TempPkt.PayloadLen = len;
    if (len > LORA_MAX_PAYLOAD_LEN) len = LORA_MAX_PAYLOAD_LEN;
    memcpy(s_TempPkt.Payload, payload, len);
    
    uint16_t packed_len = LoRa_Manager_Protocol_Pack(&s_TempPkt, s_FSM.pending_buf, sizeof(s_FSM.pending_buf), s_FSM_Config->tmode, s_FSM_Config->channel);
    if (packed_len == 0) return false;
    
    memcpy(s_FSM.pending_buf, &s_TempPkt, sizeof(LoRa_Packet_t));
    s_FSM.pending_len = 1; 
    
    // 保存当前 ID
    s_FSM.current_tx_id = msg_id;

    return LoRa_Manager_Buffer_PushTx(&s_TempPkt, s_FSM_Config->tmode, s_FSM_Config->channel, scratch_buf, scratch_len);
}

bool LoRa_Manager_FSM_ProcessRxPacket(const LoRa_Packet_t *packet) {
    if (packet->IsAckPacket) {
        if (s_FSM.state == LORA_FSM_WAIT_ACK) {
            LoRa_Packet_t *pending = (LoRa_Packet_t*)s_FSM.pending_buf;
            if (packet->Sequence == pending->Sequence) {
                LORA_LOG("[MGR] ACK Recv (Seq %d)\r\n", packet->Sequence);
                
                // [修复] 收到 ACK，设置挂起事件，通知 Manager 发送成功
                // 注意：必须在 Reset 之前保存 ID，因为 Reset 会清空 ID
                _SetPendingEvent(FSM_EVT_TX_DONE, s_FSM.current_tx_id);
                
                _FSM_Reset();
            }
        }
        return false; 
    } else {
        // 数据包去重检查
        if (_FSM_CheckDuplicate(packet->SourceID, packet->Sequence)) {
            LORA_LOG("[MGR] Drop Duplicate\r\n");
            // 即使是重复包，如果是需要 ACK 的，也得回 ACK (可能上一个 ACK 丢了)
            if (packet->NeedAck && packet->TargetID != 0xFFFF) {
                s_FSM.ack_ctx.target_id = packet->SourceID;
                s_FSM.ack_ctx.seq = packet->Sequence;
                s_FSM.ack_ctx.pending = true;
                _FSM_SetState(LORA_FSM_ACK_DELAY, LORA_ACK_DELAY_MS);
            }
            return false; 
        }
        
        // 新包
        if (packet->NeedAck && packet->TargetID != 0xFFFF) {
            s_FSM.ack_ctx.target_id = packet->SourceID;
            s_FSM.ack_ctx.seq = packet->Sequence;
            s_FSM.ack_ctx.pending = true;
            _FSM_SetState(LORA_FSM_ACK_DELAY, LORA_ACK_DELAY_MS);
        }
        return true; 
    }
}

/**
 * @brief  运行状态机 (周期调用)
 * @param  scratch_buf: 共享工作区 (用于 TX 预览)
 * @param  scratch_len: 工作区大小
 * @return FSM 输出事件 (上层需根据此返回值触发回调)
 */
LoRa_FSM_Output_t LoRa_Manager_FSM_Run(uint8_t *scratch_buf, uint16_t scratch_len) {
    // ============================================================
    // 1. 输入采集 (Input Collection)
    // ============================================================
    LoRa_FSM_Output_t output = { .Event = FSM_EVT_NONE, .MsgID = 0 };
    uint32_t now = OSAL_GetTick();
    
    // 计算是否超时
    bool is_timeout = false;
    if (s_FSM.timeout_deadline != LORA_TIMEOUT_INFINITE) {
        // 使用 int32_t 强转处理 tick 溢出回绕问题
        if ((int32_t)(s_FSM.timeout_deadline - now) <= 0) {
            is_timeout = true;
        }
    }

    // ============================================================
    // 2. 异步事件分发 (Async Event Dispatch)
    // ============================================================
    // 优先处理来自 ProcessRxPacket 的挂起事件 (例如收到 ACK 导致的 TX_DONE)
    if (s_PendingOutput.Event != FSM_EVT_NONE) {
        output = s_PendingOutput;
        s_PendingOutput.Event = FSM_EVT_NONE; // 清除挂起标志
        return output; // 立即返回，优先响应
    }

    // ============================================================
    // 3. 状态机核心逻辑 (State Machine Core)
    // ============================================================
    switch (s_FSM.state) {
        
        // --------------------------------------------------------
        // 状态: 空闲 (IDLE)
        // 任务: 检查发送队列，调度新任务
        // --------------------------------------------------------
        case LORA_FSM_IDLE: {
            bool need_ack = false;
            
            // 尝试从队列中提取并发送数据
            if (_FSM_Action_PhyTxScheduler(scratch_buf, scratch_len, &need_ack)) {
                // 获取刚刚发送的包信息（用于判断下一步状态）
                LoRa_Packet_t *pending = (LoRa_Packet_t*)s_FSM.pending_buf;
                
                if (pending->TargetID == LORA_ID_BROADCAST) {
                    // [分支1] 广播模式：进入盲发状态
                    s_FSM.retry_count = 0;
                    _FSM_SetState(LORA_FSM_BROADCAST_RUN, LORA_BROADCAST_INTERVAL);
                    LORA_LOG("[MGR] Broadcast Start\r\n");
                }
                else if (need_ack) {
                    // [分支2] 可靠传输模式：进入等待 ACK 状态
                    s_FSM.retry_count = 0;
                    _FSM_SetState(LORA_FSM_WAIT_ACK, LORA_ACK_TIMEOUT_MS);
                    LORA_LOG("[MGR] Wait ACK...\r\n");
                }
                else {
                    // [分支3] 单播不可靠模式：发送即成功
                    output.Event = FSM_EVT_TX_DONE;
                    output.MsgID = s_FSM.current_tx_id;
                    _FSM_Reset(); // 任务完成，重置状态
                }
            }
            break;
        }

        // --------------------------------------------------------
        // 状态: ACK 延时 (ACK_DELAY)
        // 任务: 等待延时结束，发送 ACK 包
        // --------------------------------------------------------
        case LORA_FSM_ACK_DELAY: {
            if (is_timeout) {
                if (s_FSM.ack_ctx.pending) {
                    _FSM_SendAck(); 
                    LORA_LOG("[MGR] ACK Queued\r\n");
                }
                // 发送完 ACK 后回到空闲状态
                _FSM_SetState(LORA_FSM_IDLE, LORA_TIMEOUT_INFINITE);
            }
            break;
        }

        // --------------------------------------------------------
        // 状态: 等待 ACK (WAIT_ACK)
        // 任务: 检查超时，执行重传或报错
        // --------------------------------------------------------
        case LORA_FSM_WAIT_ACK: {
            if (is_timeout) {
                _FSM_HandleAckTimeout(scratch_buf, scratch_len, &output);
            }
            break;
        }

        // --------------------------------------------------------
        // 状态: 广播盲发 (BROADCAST_RUN)
        // 任务: 循环发送多次，提高送达率
        // --------------------------------------------------------
        case LORA_FSM_BROADCAST_RUN: {
            if (is_timeout) {
                if (s_FSM.retry_count < LORA_BROADCAST_REPEAT) {
                    // [重发逻辑]
                    s_FSM.retry_count++;
                    if (s_FSM.pending_len > 0) {
                        LoRa_Packet_t *pending = (LoRa_Packet_t*)s_FSM.pending_buf;
                        LoRa_Manager_Buffer_PushTx(pending, s_FSM_Config->tmode, s_FSM_Config->channel, scratch_buf, scratch_len);
                        
                        // 尝试立即调度
                        LoRa_FSM_State_t backup_state = s_FSM.state;
                        s_FSM.state = LORA_FSM_IDLE;
                        
                        if (_FSM_Action_PhyTxScheduler(scratch_buf, scratch_len, NULL)) {
                            // 发送成功，设置下一次间隔
                            _FSM_SetState(LORA_FSM_BROADCAST_RUN, LORA_BROADCAST_INTERVAL);
                        } else {
                            s_FSM.state = backup_state;
                        }
                    }
                } else {
                    // [完成逻辑] 广播结束，视为成功
                    output.Event = FSM_EVT_TX_DONE;
                    output.MsgID = s_FSM.current_tx_id;
                    _FSM_Reset();
                }
            }
            break;
        }

        default:
            break;
    }
    
    return output;
}

