#include "lora_manager.h"
#include "lora_manager_fsm.h"
#include "lora_manager_buffer.h"
#include "lora_osal.h"
#include <string.h>

// ============================================================
//                    内部变量
// ============================================================

#define RX_WORKSPACE_SIZE  MGR_RX_BUF_SIZE 
static uint8_t s_RxWorkspace[RX_WORKSPACE_SIZE];

// 保存回调结构体
static LoRa_Manager_Callback_t s_MgrCb = { NULL, NULL };

static const LoRa_Config_t *s_Mgr_Config = NULL;
static const LoRa_Cipher_t *s_Cipher = NULL;

static LoRa_MsgID_t s_NextMsgID = 1;

// 发送缓存队列
#define TX_PACKET_QUEUE_SIZE 4 

typedef struct {
    uint8_t  payload[LORA_MAX_PAYLOAD_LEN];
    uint16_t len;
    uint16_t target_id;
    LoRa_SendOpt_t opt;
    LoRa_MsgID_t msg_id; 
} TxRequest_t;

static TxRequest_t s_TxQueue[TX_PACKET_QUEUE_SIZE];
static uint8_t s_TxQ_Head = 0;
static uint8_t s_TxQ_Tail = 0;
static uint8_t s_TxQ_Count = 0;

// ============================================================
//                    核心实现
// ============================================================

void LoRa_Manager_Init(const LoRa_Config_t *cfg, const LoRa_Manager_Callback_t *cb) {
    LORA_CHECK_VOID(cfg); 
    s_Mgr_Config = cfg;
    
    if (cb) {
        s_MgrCb = *cb; // 拷贝结构体内容
    } else {
        s_MgrCb.OnRecv = NULL;
        s_MgrCb.OnTxResult = NULL;
    }
    
    s_Cipher = NULL;
    
    // 初始化队列
    s_TxQ_Head = 0;
    s_TxQ_Tail = 0;
    s_TxQ_Count = 0;
    s_NextMsgID = 1; 
    
    LoRa_Manager_Buffer_Init();
    LoRa_Manager_FSM_Init(cfg); 
}

void LoRa_Manager_RegisterCipher(const LoRa_Cipher_t *cipher) {
    s_Cipher = cipher;
}

static void _ProcessTxQueue(void) {
    if (LoRa_Manager_FSM_IsBusy()) return;
    if (s_TxQ_Count == 0) return;
    
    TxRequest_t *req = &s_TxQueue[s_TxQ_Tail];
    
    uint8_t tx_stack_buf[LORA_MAX_PAYLOAD_LEN + 32];
    
    if (LoRa_Manager_FSM_Send(req->payload, req->len, req->target_id, req->opt, req->msg_id, tx_stack_buf, sizeof(tx_stack_buf))) {
        s_TxQ_Tail = (s_TxQ_Tail + 1) % TX_PACKET_QUEUE_SIZE;
        s_TxQ_Count--;
        LORA_LOG("[MGR] Dequeue TX (ID:%d, Left:%d)\r\n", req->msg_id, s_TxQ_Count);
    }
}

void LoRa_Manager_Run(void) {
    // 1. 从 Port 拉取数据
    LoRa_Manager_Buffer_PullFromPort();
    
    // 2. 解析数据包
    LoRa_Packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    
    if (s_Mgr_Config && LoRa_Manager_Buffer_GetRxPacket(&pkt, s_Mgr_Config->net_id, s_Mgr_Config->group_id, 
                                        s_RxWorkspace, RX_WORKSPACE_SIZE)) {
        
        // 调用 FSM 处理 (去重、ACK识别)
        bool valid_new_packet = LoRa_Manager_FSM_ProcessRxPacket(&pkt);
        
        // 如果是有效新包，回调上层
        if (valid_new_packet && s_MgrCb.OnRecv) {
            if (s_Cipher && s_Cipher->Decrypt && pkt.PayloadLen > 0) {
                uint16_t new_len = s_Cipher->Decrypt(pkt.Payload, pkt.PayloadLen, pkt.Payload);
                pkt.PayloadLen = new_len;
            }
            s_MgrCb.OnRecv(pkt.Payload, pkt.PayloadLen, pkt.SourceID);
        }
    }
    
    // 3. 运行状态机并处理事件
    LoRa_FSM_Output_t fsm_out = LoRa_Manager_FSM_Run(s_RxWorkspace, RX_WORKSPACE_SIZE);
    
    if (fsm_out.Event != FSM_EVT_NONE) {
        switch (fsm_out.Event) {
            case FSM_EVT_TX_DONE:
                if (s_MgrCb.OnTxResult) {
                    s_MgrCb.OnTxResult(fsm_out.MsgID, true);
                }
                break;
                
            case FSM_EVT_TX_TIMEOUT:
                if (s_MgrCb.OnTxResult) {
                    s_MgrCb.OnTxResult(fsm_out.MsgID, false);
                }
                break;
                
            default:
                break;
        }
    }
    
    // 4. 处理发送队列
    _ProcessTxQueue();
}

LoRa_MsgID_t LoRa_Manager_Send(const uint8_t *payload, uint16_t len, uint16_t target_id, LoRa_SendOpt_t opt) {
    static uint8_t s_FinalPayload[LORA_MAX_PAYLOAD_LEN];
    uint16_t final_len = len;
    
    if (len > LORA_MAX_PAYLOAD_LEN) return 0;

    if (s_Cipher && s_Cipher->Encrypt) {
        final_len = s_Cipher->Encrypt(payload, len, s_FinalPayload);
        if (final_len > LORA_MAX_PAYLOAD_LEN) return 0; 
    } else {
        memcpy(s_FinalPayload, payload, len);
    }

    uint32_t ctx = OSAL_EnterCritical();
    
    if (s_TxQ_Count >= TX_PACKET_QUEUE_SIZE) {
        OSAL_ExitCritical(ctx);
        LORA_LOG("[MGR] TX Queue Full!\r\n");
        return 0;
    }
    
    TxRequest_t *req = &s_TxQueue[s_TxQ_Head];
    memcpy(req->payload, s_FinalPayload, final_len);
    req->len = final_len;
    req->target_id = target_id;
    req->opt = opt; 
    
    req->msg_id = s_NextMsgID++;
    if (s_NextMsgID == 0) s_NextMsgID = 1; 
    
    LoRa_MsgID_t ret_id = req->msg_id;
    
    s_TxQ_Head = (s_TxQ_Head + 1) % TX_PACKET_QUEUE_SIZE;
    s_TxQ_Count++;
    
    OSAL_ExitCritical(ctx);
    
    _ProcessTxQueue();
    
    return ret_id;
}

bool LoRa_Manager_IsBusy(void) {
    return LoRa_Manager_FSM_IsBusy() || (s_TxQ_Count > 0);
}

uint32_t LoRa_Manager_GetSleepDuration(void) {
    if (s_TxQ_Count > 0) return 0;
    return LoRa_Manager_FSM_GetNextTimeout();
}
