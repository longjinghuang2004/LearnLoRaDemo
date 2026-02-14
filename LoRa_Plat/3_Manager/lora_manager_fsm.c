/**
  ******************************************************************************
  * @file    lora_manager_fsm.c
  * @author  LoRaPlat Team
  * @brief   LoRa 协议状态机实现
  ******************************************************************************
  */

#include "lora_manager_fsm.h"
#include "lora_manager_buffer.h"
#include "lora_port.h"
#include "lora_osal.h"
#include "lora_service.h" // 获取 Config
#include <string.h> // [新增] 修复 memset/memcpy 警告

// 内部状态上下文
typedef struct {
    LoRa_FSM_State_t state;
    uint32_t         timer_start;
    uint8_t          retry_count;
    uint8_t          tx_seq; // 当前发送序号
    
    // 待发送的 ACK 信息
    struct {
        bool     pending;
        uint16_t target_id;
        uint8_t  seq;
    } ack_ctx;
    
} FSM_Context_t;

static FSM_Context_t s_FSM;

// ============================================================
//                    1. 内部辅助
// ============================================================

static void _FSM_Reset(void) {
    s_FSM.state = LORA_FSM_IDLE;
    s_FSM.ack_ctx.pending = false;
}

static void _FSM_SendAck(void) {
    LoRa_Packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    
    pkt.IsAckPacket = true;
    pkt.NeedAck = false;
    pkt.HasCrc = LORA_ENABLE_CRC;
    pkt.TargetID = s_FSM.ack_ctx.target_id;
    pkt.SourceID = LoRa_Service_GetConfig()->net_id;
    pkt.Sequence = s_FSM.ack_ctx.seq;
    pkt.PayloadLen = 0;
    
    // 推入队列
    const LoRa_Config_t *cfg = LoRa_Service_GetConfig();
    LoRa_Manager_Buffer_PushTx(&pkt, cfg->tmode, cfg->channel);
    
    s_FSM.ack_ctx.pending = false;
}

// ============================================================
//                    2. 核心接口实现
// ============================================================

void LoRa_Manager_FSM_Init(void) {
    _FSM_Reset();
    s_FSM.tx_seq = 0;
}

bool LoRa_Manager_FSM_Send(const uint8_t *payload, uint16_t len, uint16_t target_id) {
    LoRa_Packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    
    pkt.IsAckPacket = false;
    pkt.NeedAck = LORA_ENABLE_ACK;
    pkt.HasCrc = LORA_ENABLE_CRC;
    pkt.TargetID = target_id;
    pkt.SourceID = LoRa_Service_GetConfig()->net_id;
    pkt.Sequence = ++s_FSM.tx_seq;
    pkt.PayloadLen = len;
    memcpy(pkt.Payload, payload, len);
    
    const LoRa_Config_t *cfg = LoRa_Service_GetConfig();
    return LoRa_Manager_Buffer_PushTx(&pkt, cfg->tmode, cfg->channel);
}

void LoRa_Manager_FSM_ProcessRxPacket(const LoRa_Packet_t *packet) {
    // 1. 如果是 ACK 包
    if (packet->IsAckPacket) {
        if (s_FSM.state == LORA_FSM_WAIT_ACK) {
            // 简单校验：ACK 的 Seq 应该等于我们发的 Seq
            // 这里简化处理，只要收到 ACK 就认为成功
            LORA_LOG("[MGR] ACK Recv\r\n");
            _FSM_Reset();
            // TODO: 通知上层发送成功
        }
    } 
    // 2. 如果是数据包
    else {
        // TODO: 通知上层收到数据 (Callback)
        // 这里需要回调 Service 层，但 FSM 不直接依赖 Service 的回调
        // 我们在 Manager Core 里处理回调，这里只处理协议逻辑
        
        // 如果需要回复 ACK
        if (packet->NeedAck && packet->TargetID != 0xFFFF) {
            s_FSM.ack_ctx.target_id = packet->SourceID;
            s_FSM.ack_ctx.seq = packet->Sequence;
            s_FSM.ack_ctx.pending = true;
            
            s_FSM.state = LORA_FSM_ACK_DELAY;
            s_FSM.timer_start = OSAL_GetTick();
        }
    }
}

void LoRa_Manager_FSM_Run(void) {
    uint32_t now = OSAL_GetTick();
    
    // 1. 尝试物理发送 (从队列取数据推给 Port)
    if (LoRa_Manager_Buffer_HasTxData()) {
        // 只有当 Port 空闲时才发送
        if (!LoRa_Port_IsTxBusy()) {
            uint8_t temp_buf[256];
            uint16_t len = LoRa_Manager_Buffer_PeekTx(temp_buf, sizeof(temp_buf));
            
            if (LoRa_Port_TransmitData(temp_buf, len) > 0) {
                // 发送成功，移除队列
                LoRa_Manager_Buffer_PopTx(len);
                
                // 如果当前是 IDLE，且发送的是需要 ACK 的数据包...
                // 这里有个问题：Buffer 里存的是字节流，我们不知道它是否需要 ACK。
                // 这是一个设计取舍。为了简化，我们假设所有应用层数据都需要 ACK。
                // 或者，我们在 FSM 状态里记录“正在发送的数据是否需要 ACK”。
                
                // 简化逻辑：如果进入 TX_RUNNING，我们假设它可能需要等待。
                // 但实际上，Buffer 里的数据可能是 ACK 包（不需要回复），也可能是 Data 包。
                // 暂时策略：发送后统一进入 IDLE，除非...
                
                // 修正：FSM 应该控制发送流程。
                // 如果是 Data 包，发送后进入 WAIT_ACK。
                // 如果是 ACK 包，发送后进入 IDLE。
                // 由于 Buffer 丢失了元数据，我们这里无法区分。
                
                // 解决方案：在 Step 2 中，我们先不实现复杂的重传逻辑。
                // 发送完就当成功。
                // 等后续优化 Buffer 结构（存 Packet 而不是 Bytes）再完善。
            }
        }
    }
    
    // 2. 状态机逻辑
    switch (s_FSM.state) {
        case LORA_FSM_ACK_DELAY:
            if (now - s_FSM.timer_start > LORA_ACK_DELAY_MS) {
                _FSM_SendAck();
                s_FSM.state = LORA_FSM_IDLE;
            }
            break;
            
        // ... 其他状态暂略，等待 Buffer 完善后补充 ...
    }
}

bool LoRa_Manager_FSM_IsBusy(void) {
    return s_FSM.state != LORA_FSM_IDLE;
}
