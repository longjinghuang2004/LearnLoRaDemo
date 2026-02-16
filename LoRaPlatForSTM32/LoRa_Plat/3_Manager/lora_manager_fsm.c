#include "lora_manager_fsm.h"
#include "lora_manager_buffer.h"
#include "lora_port.h"
#include "lora_osal.h"
#include "lora_manager.h" 
#include "lora_service.h" // [新增] 用于 NotifyEvent

#include <string.h>

// ============================================================
//                    1. 内部状态定义
// ============================================================

static const LoRa_Config_t *s_FSM_Config = NULL;

// 去重表条目
typedef struct {
    uint16_t src_id;
    uint8_t  seq;
    uint32_t last_seen; 
    bool     valid;
} DeDupEntry_t;

typedef struct {
    LoRa_FSM_State_t state;
    uint32_t         timeout_deadline; 
    uint8_t          retry_count;
    uint8_t          tx_seq; 
    
    // [新增] 当前正在处理的消息 ID
    LoRa_MsgID_t     current_tx_id;
    
    // --- 重传/广播上下文 ---
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
//                    2. 内部辅助函数 (Actions)
// ============================================================

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
    s_FSM.current_tx_id = 0; // 清除 ID
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

static bool _FSM_CheckDuplicate(uint16_t src_id, uint8_t seq) {
    uint32_t now = OSAL_GetTick();
    int lru_idx = 0;
    uint32_t min_time = 0xFFFFFFFF;
    
    for (int i = 0; i < LORA_DEDUP_MAX_COUNT; i++) {
        if (s_FSM.dedup_table[i].valid) {
            if (s_FSM.dedup_table[i].src_id == src_id) {
                if (s_FSM.dedup_table[i].seq == seq) {
                    s_FSM.dedup_table[i].last_seen = now; 
                    return true; 
                } else {
                    s_FSM.dedup_table[i].seq = seq;
                    s_FSM.dedup_table[i].last_seen = now;
                    return false; 
                }
            }
            if (s_FSM.dedup_table[i].last_seen < min_time) {
                min_time = s_FSM.dedup_table[i].last_seen;
                lru_idx = i;
            }
        } else {
            lru_idx = i;
            break; 
        }
    }
    
    s_FSM.dedup_table[lru_idx].valid = true;
    s_FSM.dedup_table[lru_idx].src_id = src_id;
    s_FSM.dedup_table[lru_idx].seq = seq;
    s_FSM.dedup_table[lru_idx].last_seen = now;
    
    return false; 
}

// ============================================================
//                    3. 状态处理函数 (State Handlers)
// ============================================================

static bool _FSM_Action_PhyTxScheduler(uint8_t *scratch_buf, uint16_t scratch_len, bool *is_need_ack) {
    if (LoRa_Port_IsTxBusy()) return false;
    
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

static void _FSM_State_AckDelay(uint32_t now) {
    if ((int32_t)(s_FSM.timeout_deadline - now) <= 0) {
        if (s_FSM.ack_ctx.pending) {
            _FSM_SendAck(); 
            LORA_LOG("[MGR] ACK Queued\r\n");
        }
        _FSM_SetState(LORA_FSM_IDLE, LORA_TIMEOUT_INFINITE);
    }
}

static void _FSM_State_WaitAck(uint32_t now, uint8_t *scratch_buf, uint16_t scratch_len) {
    if ((int32_t)(s_FSM.timeout_deadline - now) <= 0) {
        if (s_FSM.retry_count < LORA_MAX_RETRY) {
            s_FSM.retry_count++;
            LORA_LOG("[MGR] ACK Timeout, Retry %d\r\n", s_FSM.retry_count);
            
            if (s_FSM.pending_len > 0) {
                LoRa_Packet_t *pending = (LoRa_Packet_t*)s_FSM.pending_buf;
                LoRa_Manager_Buffer_PushTx(pending, s_FSM_Config->tmode, s_FSM_Config->channel, scratch_buf, scratch_len);
                
                LoRa_FSM_State_t backup_state = s_FSM.state;
                s_FSM.state = LORA_FSM_IDLE;
                if (_FSM_Action_PhyTxScheduler(scratch_buf, scratch_len, NULL)) {
                    _FSM_SetState(LORA_FSM_WAIT_ACK, LORA_RETRY_INTERVAL_MS);
                } else {
                    s_FSM.state = backup_state; 
                }
            } else {
                _FSM_Reset();
            }
        } else {
            LORA_LOG("[MGR] ACK Failed (Max Retry)\r\n");
            // [新增] 触发失败事件
            LoRa_Service_NotifyEvent(LORA_EVENT_TX_FAILED_ID, (void*)&s_FSM.current_tx_id);
            _FSM_Reset();
        }
    }
}

static void _FSM_State_BroadcastRun(uint32_t now, uint8_t *scratch_buf, uint16_t scratch_len) {
    if ((int32_t)(s_FSM.timeout_deadline - now) <= 0) {
        if (s_FSM.retry_count < LORA_BROADCAST_REPEAT) {
            s_FSM.retry_count++;
            if (s_FSM.pending_len > 0) {
                LoRa_Packet_t *pending = (LoRa_Packet_t*)s_FSM.pending_buf;
                LoRa_Manager_Buffer_PushTx(pending, s_FSM_Config->tmode, s_FSM_Config->channel, scratch_buf, scratch_len);
                
                LoRa_FSM_State_t backup_state = s_FSM.state;
                s_FSM.state = LORA_FSM_IDLE;
                if (_FSM_Action_PhyTxScheduler(scratch_buf, scratch_len, NULL)) {
                    _FSM_SetState(LORA_FSM_BROADCAST_RUN, LORA_BROADCAST_INTERVAL);
                } else {
                    s_FSM.state = backup_state;
                }
            }
        } else {
            // 广播结束
            // [新增] 触发成功事件 (广播视为成功)
            LoRa_Service_NotifyEvent(LORA_EVENT_TX_SUCCESS_ID, (void*)&s_FSM.current_tx_id);
            _FSM_Reset();
        }
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

// [修改] 增加 msg_id 参数
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
    
    if (target_id == 0xFFFF) {
        s_TempPkt.NeedAck = false;
    } else {
        s_TempPkt.NeedAck = opt.NeedAck;
    }
    
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
    
    // [新增] 保存当前 ID
    s_FSM.current_tx_id = msg_id;

    return LoRa_Manager_Buffer_PushTx(&s_TempPkt, s_FSM_Config->tmode, s_FSM_Config->channel, scratch_buf, scratch_len);
}

bool LoRa_Manager_FSM_ProcessRxPacket(const LoRa_Packet_t *packet) {
    if (packet->IsAckPacket) {
        if (s_FSM.state == LORA_FSM_WAIT_ACK) {
            LoRa_Packet_t *pending = (LoRa_Packet_t*)s_FSM.pending_buf;
            if (packet->Sequence == pending->Sequence) {
                LORA_LOG("[MGR] ACK Recv (Seq %d)\r\n", packet->Sequence);
                // [新增] 触发成功事件
                LoRa_Service_NotifyEvent(LORA_EVENT_TX_SUCCESS_ID, (void*)&s_FSM.current_tx_id);
                _FSM_Reset(); 
            }
        }
        return false; 
    } else {
        if (_FSM_CheckDuplicate(packet->SourceID, packet->Sequence)) {
            LORA_LOG("[MGR] Drop Duplicate\r\n");
            if (packet->NeedAck && packet->TargetID != 0xFFFF) {
                s_FSM.ack_ctx.target_id = packet->SourceID;
                s_FSM.ack_ctx.seq = packet->Sequence;
                s_FSM.ack_ctx.pending = true;
                _FSM_SetState(LORA_FSM_ACK_DELAY, LORA_ACK_DELAY_MS);
            }
            return false; 
        }
        
        if (packet->NeedAck && packet->TargetID != 0xFFFF) {
            s_FSM.ack_ctx.target_id = packet->SourceID;
            s_FSM.ack_ctx.seq = packet->Sequence;
            s_FSM.ack_ctx.pending = true;
            _FSM_SetState(LORA_FSM_ACK_DELAY, LORA_ACK_DELAY_MS);
        }
        return true; 
    }
}

void LoRa_Manager_FSM_Run(uint8_t *scratch_buf, uint16_t scratch_len) {
    uint32_t now = OSAL_GetTick();
    
    if (s_FSM.state == LORA_FSM_IDLE) {
        bool need_ack = false;
        if (_FSM_Action_PhyTxScheduler(scratch_buf, scratch_len, &need_ack)) {
            LoRa_Packet_t *pending = (LoRa_Packet_t*)s_FSM.pending_buf;
            
            if (pending->TargetID == 0xFFFF) {
                s_FSM.retry_count = 0;
                LORA_LOG("[MGR] Broadcast Start\r\n");
                _FSM_SetState(LORA_FSM_BROADCAST_RUN, LORA_BROADCAST_INTERVAL);
            }
            else if (need_ack) {
                s_FSM.retry_count = 0;
                LORA_LOG("[MGR] Wait ACK...\r\n");
                _FSM_SetState(LORA_FSM_WAIT_ACK, LORA_ACK_TIMEOUT_MS);
            }
            else {
                // [新增] 不需要 ACK 的单播，发完即成功
                LoRa_Service_NotifyEvent(LORA_EVENT_TX_SUCCESS_ID, (void*)&s_FSM.current_tx_id);
                _FSM_Reset();
            }
        }
    }
    
    switch (s_FSM.state) {
        case LORA_FSM_ACK_DELAY:
            _FSM_State_AckDelay(now);
            break;
            
        case LORA_FSM_WAIT_ACK:
            _FSM_State_WaitAck(now, scratch_buf, scratch_len);
            break;
            
        case LORA_FSM_BROADCAST_RUN:
            _FSM_State_BroadcastRun(now, scratch_buf, scratch_len);
            break;
            
        default:
            break;
    }
}

bool LoRa_Manager_FSM_IsBusy(void) {
    return s_FSM.state != LORA_FSM_IDLE;
}
