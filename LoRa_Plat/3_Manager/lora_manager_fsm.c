/**
  ******************************************************************************
  * @file    lora_manager_fsm.c
  * @author  LoRaPlat Team
  * @brief   LoRa 协议状态机实现 (V3.4 严格停等协议版)
  *          1. 修复了发送逻辑，确保在等待 ACK 期间拒绝新数据。
  *          2. 增加了 HexDump 调试打印。
  *          3. 使用共享工作区优化内存。
  ******************************************************************************
  */

#include "lora_manager_fsm.h"
#include "lora_manager_buffer.h"
#include "lora_port.h"
#include "lora_osal.h"
#include "lora_service.h" // 获取 Config
#include <string.h>

// ============================================================
//                    1. 内部状态定义
// ============================================================

typedef struct {
    LoRa_FSM_State_t state;
    uint32_t         timer_start;
    uint8_t          retry_count;
    uint8_t          tx_seq; // 当前发送序号
    
    // 待发送的 ACK 信息上下文
    struct {
        bool     pending;
        uint16_t target_id;
        uint8_t  seq;
    } ack_ctx;
    
} FSM_Context_t;

static FSM_Context_t s_FSM;

// ============================================================
//                    2. 内部辅助函数
// ============================================================

static void _FSM_Reset(void) {
    s_FSM.state = LORA_FSM_IDLE;
    s_FSM.ack_ctx.pending = false;
    s_FSM.retry_count = 0;
}

/**
 * @brief 发送 ACK 包
 * @note  ACK 包也是一种数据包，通过 Buffer 机制发送。
 *        调用此函数后，ACK 被推入队列，状态机切回 IDLE，
 *        Run 函数会在下一次循环中将其发出。
 */
static void _FSM_SendAck(uint8_t *scratch_buf, uint16_t scratch_len) {
    LoRa_Packet_t pkt;
    
    // 清空结构体防止脏数据
    memset(&pkt, 0, sizeof(pkt));
    
    pkt.IsAckPacket = true;
    pkt.NeedAck = false; // ACK 包本身不需要 ACK
    pkt.HasCrc = LORA_ENABLE_CRC;
    pkt.TargetID = s_FSM.ack_ctx.target_id;
    pkt.SourceID = LoRa_Service_GetConfig()->net_id;
    pkt.Sequence = s_FSM.ack_ctx.seq; // 回复对方的序号
    pkt.PayloadLen = 0;
    
    // 推入发送队列 (使用共享缓冲区)
    const LoRa_Config_t *cfg = LoRa_Service_GetConfig();
    LoRa_Manager_Buffer_PushTx(&pkt, cfg->tmode, cfg->channel, scratch_buf, scratch_len);
    
    s_FSM.ack_ctx.pending = false;
}

// ============================================================
//                    3. 核心接口实现
// ============================================================

void LoRa_Manager_FSM_Init(void) {
    _FSM_Reset();
    s_FSM.tx_seq = 0;
}

bool LoRa_Manager_FSM_Send(const uint8_t *payload, uint16_t len, uint16_t target_id,
                           uint8_t *scratch_buf, uint16_t scratch_len) {
    
    // 【关键修复】严格停等协议检查
    // 如果状态机不为空闲（例如正在等待上一包的 ACK），则拒绝发送新数据。
    // 这样保证了上层业务逻辑必须等待底层传输完成。
    if (s_FSM.state != LORA_FSM_IDLE) {
        LORA_LOG("[MGR] Send Reject: Busy (State %d)\r\n", s_FSM.state);
        return false; 
    }

    LoRa_Packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    
    pkt.IsAckPacket = false;
    // 广播包 (0xFFFF) 不需要 ACK，否则会导致信道风暴
    pkt.NeedAck = (LORA_ENABLE_ACK && target_id != 0xFFFF); 
    pkt.HasCrc = LORA_ENABLE_CRC;
    pkt.TargetID = target_id;
    pkt.SourceID = LoRa_Service_GetConfig()->net_id;
    pkt.Sequence = ++s_FSM.tx_seq;
    pkt.PayloadLen = len;
    
    // 安全拷贝 Payload
    if (len > LORA_MAX_PAYLOAD_LEN) len = LORA_MAX_PAYLOAD_LEN;
    memcpy(pkt.Payload, payload, len);
    
    const LoRa_Config_t *cfg = LoRa_Service_GetConfig();
    
    // 尝试推入队列
    // 注意：这里返回 true 仅代表入队成功，实际发送由 Run 函数调度
    return LoRa_Manager_Buffer_PushTx(&pkt, cfg->tmode, cfg->channel, scratch_buf, scratch_len);
}

void LoRa_Manager_FSM_ProcessRxPacket(const LoRa_Packet_t *packet) {
    // 1. 如果是 ACK 包
    if (packet->IsAckPacket) {
        if (s_FSM.state == LORA_FSM_WAIT_ACK) {
            // 简单校验：ACK 的 Seq 应该等于我们发的 Seq (这里简化处理，Phase 2 可增强)
            // 只要收到 ACK 就认为成功，复位状态机
            LORA_LOG("[MGR] ACK Recv (Seq %d)\r\n", packet->Sequence);
            _FSM_Reset();
            // TODO: 通知上层发送成功 (Event Callback)
        }
    } 
    // 2. 如果是数据包
    else {
        // 如果对方要求 ACK，且不是广播包 (TargetID != 0xFFFF)
        if (packet->NeedAck && packet->TargetID != 0xFFFF) {
            // 记录需要回复的上下文
            s_FSM.ack_ctx.target_id = packet->SourceID;
            s_FSM.ack_ctx.seq = packet->Sequence;
            s_FSM.ack_ctx.pending = true;
            
            // 进入 ACK 延时状态 (避免半双工冲突)
            s_FSM.state = LORA_FSM_ACK_DELAY;
            s_FSM.timer_start = OSAL_GetTick();
        }
    }
}

void LoRa_Manager_FSM_Run(uint8_t *scratch_buf, uint16_t scratch_len) {
    uint32_t now = OSAL_GetTick();
    
    // --------------------------------------------------------
    // 1. 物理层发送调度 (Physical TX Scheduler)
    // --------------------------------------------------------
    // 【关键修复】只有当状态机处于 IDLE 时，才允许从队列提取新数据发送。
    // 这确保了如果正在等待 ACK，不会发送下一包数据。
    if (s_FSM.state == LORA_FSM_IDLE && LoRa_Manager_Buffer_HasTxData()) {
        
        // 只有当 Port 空闲时才发送
        if (!LoRa_Port_IsTxBusy()) {
            // 使用共享缓冲区 Peek 数据
            uint16_t len = LoRa_Manager_Buffer_PeekTx(scratch_buf, scratch_len);
            
            if (len > 0) {
                // [新增] 打印发送原始数据 (HexDump)
                LORA_HEXDUMP("TX RAW", scratch_buf, len);

                // 尝试启动 DMA 发送
                if (LoRa_Port_TransmitData(scratch_buf, len) > 0) {
                    // 发送成功，从队列移除数据
                    LoRa_Manager_Buffer_PopTx(len);
                    
                    // 触发发送完成事件
                    LoRa_Service_NotifyEvent(LORA_EVENT_MSG_SENT, NULL);
                    
                    // 【关键逻辑】检查是否需要进入等待 ACK 状态
                    // 我们需要解析刚才发出去的包头中的 Ctrl 字节
                    const LoRa_Config_t *cfg = LoRa_Service_GetConfig();
                    
                    // 计算 Ctrl 字节在 buffer 中的偏移量
                    // TMODE=1 (定点): Target(2)+Chan(1) + Head(2)+Len(1)+Ctrl(1)... -> Ctrl在索引 6
                    // TMODE=0 (透传): Head(2)+Len(1)+Ctrl(1)... -> Ctrl在索引 3
                    uint8_t offset = (cfg->tmode == 1) ? 3 : 0;
                    
                    // 安全检查防止越界
                    if (len > offset + 3) {
                        uint8_t ctrl_byte = scratch_buf[offset + 3];
                        
                        // 检查 NeedAck 位 (0x40)
                        if (ctrl_byte & LORA_CTRL_MASK_NEED_ACK) {
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
    
    // --------------------------------------------------------
    // 2. 协议层状态机 (Protocol FSM)
    // --------------------------------------------------------
    switch (s_FSM.state) {
        case LORA_FSM_IDLE:
            // 空闲状态，无事可做
            break;
            
        case LORA_FSM_ACK_DELAY:
            // 等待延时结束，发送 ACK
            if (now - s_FSM.timer_start > LORA_ACK_DELAY_MS) {
                if (s_FSM.ack_ctx.pending) {
                    _FSM_SendAck(scratch_buf, scratch_len);
                    LORA_LOG("[MGR] ACK Queued\r\n");
                }
                // 发送 ACK 后回到 IDLE，Run 函数的下一次循环会把 ACK 发出去
                s_FSM.state = LORA_FSM_IDLE;
            }
            break;
            
        case LORA_FSM_WAIT_ACK:
            // 等待对方回复 ACK
            if (now - s_FSM.timer_start > LORA_ACK_TIMEOUT_MS) {
                // 超时处理
                if (s_FSM.retry_count < LORA_MAX_RETRY) {
                    s_FSM.retry_count++;
                    LORA_LOG("[MGR] ACK Timeout, Retry %d\r\n", s_FSM.retry_count);
                    // TODO: 触发重传 (需要 Buffer 支持保留数据，Phase 2 实现)
                    // 目前简化处理：超时直接放弃
                    _FSM_Reset();
                } else {
                    LORA_LOG("[MGR] ACK Failed (Give up)\r\n");
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
