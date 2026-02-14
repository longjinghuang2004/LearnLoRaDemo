/**
  ******************************************************************************
  * @file    lora_manager_fsm.c
  * @author  LoRaPlat Team
  * @brief   LoRa 协议状态机实现 (内存优化版 V3.3)
  *          移除了内部静态缓冲区，使用外部传入的共享工作区 (Shared Workspace)
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
 * @note  使用外部传入的 scratch_buf 进行序列化，避免栈溢出
 */
static void _FSM_SendAck(uint8_t *scratch_buf, uint16_t scratch_len) {
    LoRa_Packet_t pkt;
    
    // 清空结构体防止脏数据
    memset(&pkt, 0, sizeof(pkt));
    
    pkt.IsAckPacket = true;
    pkt.NeedAck = false;
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
    LoRa_Packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    
    pkt.IsAckPacket = false;
    pkt.NeedAck = LORA_ENABLE_ACK; // 根据配置决定是否需要 ACK
    pkt.HasCrc = LORA_ENABLE_CRC;
    pkt.TargetID = target_id;
    pkt.SourceID = LoRa_Service_GetConfig()->net_id;
    pkt.Sequence = ++s_FSM.tx_seq;
    pkt.PayloadLen = len;
    
    // 安全拷贝 Payload
    if (len > LORA_MAX_PAYLOAD_LEN) len = LORA_MAX_PAYLOAD_LEN;
    memcpy(pkt.Payload, payload, len);
    
    const LoRa_Config_t *cfg = LoRa_Service_GetConfig();
    
    // 尝试推入队列，使用共享缓冲区进行序列化
    if (LoRa_Manager_Buffer_PushTx(&pkt, cfg->tmode, cfg->channel, scratch_buf, scratch_len)) {
        // 如果需要 ACK，且不是广播包，则进入等待状态
        // 注意：目前简化逻辑，只要入队成功就认为发送流程开始
        // 真正的状态切换在 Run 函数中处理物理发送后进行会更严谨，
        // 但为了简化，我们假设入队即发送。
        // TODO: 完善重传逻辑 (Phase 2)
        return true;
    }
    
    return false;
}

void LoRa_Manager_FSM_ProcessRxPacket(const LoRa_Packet_t *packet) {
    // 1. 如果是 ACK 包
    if (packet->IsAckPacket) {
        if (s_FSM.state == LORA_FSM_WAIT_ACK) {
            // 简单校验：ACK 的 Seq 应该等于我们发的 Seq (这里简化处理)
            // 只要收到 ACK 就认为成功，复位状态机
            LORA_LOG("[MGR] ACK Recv\r\n");
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
    if (LoRa_Manager_Buffer_HasTxData()) {
        // 只有当 Port 空闲时才发送
        if (!LoRa_Port_IsTxBusy()) {
            // 使用共享缓冲区 Peek 数据 (不再定义内部 static 数组)
            uint16_t len = LoRa_Manager_Buffer_PeekTx(scratch_buf, scratch_len);
            
            if (len > 0) {
                // 尝试启动 DMA 发送
                if (LoRa_Port_TransmitData(scratch_buf, len) > 0) {
                    // 发送成功，从队列移除数据
                    LoRa_Manager_Buffer_PopTx(len);
                    
                    // 触发发送完成事件 (可选)
                    // LoRa_Service_NotifyEvent(LORA_EVENT_MSG_SENT, NULL);
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
                }
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
                    LORA_LOG("[MGR] ACK Failed\r\n");
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
