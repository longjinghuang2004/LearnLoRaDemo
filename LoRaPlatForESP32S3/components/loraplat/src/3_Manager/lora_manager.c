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

static LoRa_OnRxData_t s_OnRx = NULL;
static const LoRa_Config_t *s_Mgr_Config = NULL;
static const LoRa_Cipher_t *s_Cipher = NULL;

// [新增] 发送缓存队列 (简单的结构体数组环形队列)
#define TX_PACKET_QUEUE_SIZE 4 

typedef struct {
    uint8_t  payload[LORA_MAX_PAYLOAD_LEN];
    uint16_t len;
    uint16_t target_id;
    LoRa_SendOpt_t opt; // [新增] 保存发送选项
} TxRequest_t;

static TxRequest_t s_TxQueue[TX_PACKET_QUEUE_SIZE];
static uint8_t s_TxQ_Head = 0;
static uint8_t s_TxQ_Tail = 0;
static uint8_t s_TxQ_Count = 0;

// ============================================================
//                    核心实现
// ============================================================

void LoRa_Manager_Init(const LoRa_Config_t *cfg, LoRa_OnRxData_t on_rx) {
    LORA_CHECK_VOID(cfg); 
    s_Mgr_Config = cfg;
    s_OnRx = on_rx;
    s_Cipher = NULL;
    
    // 初始化队列
    s_TxQ_Head = 0;
    s_TxQ_Tail = 0;
    s_TxQ_Count = 0;
    
    LoRa_Manager_Buffer_Init();
    LoRa_Manager_FSM_Init(cfg); 
}

void LoRa_Manager_RegisterCipher(const LoRa_Cipher_t *cipher) {
    s_Cipher = cipher;
}

// [修改] 尝试从队列中取出并发送
static void _ProcessTxQueue(void) {
    // 1. 如果 FSM 忙，不能发送，直接退出
    if (LoRa_Manager_FSM_IsBusy()) return;
    
    // 2. 如果队列空，退出
    if (s_TxQ_Count == 0) return;
    
    // 3. 取出队首
    TxRequest_t *req = &s_TxQueue[s_TxQ_Tail];
    
    // 4. 尝试发送给 FSM
    uint8_t tx_stack_buf[LORA_MAX_PAYLOAD_LEN + 32];
    
    // [修改] 将 req->opt 传递给 FSM
    if (LoRa_Manager_FSM_Send(req->payload, req->len, req->target_id, req->opt, tx_stack_buf, sizeof(tx_stack_buf))) {
        // 发送成功，移动 Tail
        s_TxQ_Tail = (s_TxQ_Tail + 1) % TX_PACKET_QUEUE_SIZE;
        s_TxQ_Count--;
        LORA_LOG("[MGR] Dequeue TX (Left: %d)\r\n", s_TxQ_Count);
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
        
        bool valid_new_packet = LoRa_Manager_FSM_ProcessRxPacket(&pkt);
        
        if (valid_new_packet && s_OnRx) {
            if (s_Cipher && s_Cipher->Decrypt && pkt.PayloadLen > 0) {
                uint16_t new_len = s_Cipher->Decrypt(pkt.Payload, pkt.PayloadLen, pkt.Payload);
                pkt.PayloadLen = new_len;
            }
            s_OnRx(pkt.Payload, pkt.PayloadLen, pkt.SourceID);
        }
    }
    
    // 3. 运行状态机
    LoRa_Manager_FSM_Run(s_RxWorkspace, RX_WORKSPACE_SIZE);
    
    // 4. 处理发送队列
    _ProcessTxQueue();
}

bool LoRa_Manager_Send(const uint8_t *payload, uint16_t len, uint16_t target_id, LoRa_SendOpt_t opt) {
    // 1. 加密处理
    // [优化] 将栈变量改为静态变量 (200 字节)
    static uint8_t s_FinalPayload[LORA_MAX_PAYLOAD_LEN];
    uint16_t final_len = len;
    
    if (len > LORA_MAX_PAYLOAD_LEN) return false;

    if (s_Cipher && s_Cipher->Encrypt) {
        final_len = s_Cipher->Encrypt(payload, len, s_FinalPayload);
        if (final_len > LORA_MAX_PAYLOAD_LEN) return false; 
    } else {
        memcpy(s_FinalPayload, payload, len);
    }

    // 2. 尝试入队
    uint32_t ctx = OSAL_EnterCritical();
    
    if (s_TxQ_Count >= TX_PACKET_QUEUE_SIZE) {
        OSAL_ExitCritical(ctx);
        LORA_LOG("[MGR] TX Queue Full!\r\n");
        return false;
    }
    
    TxRequest_t *req = &s_TxQueue[s_TxQ_Head];
    memcpy(req->payload, s_FinalPayload, final_len); // 使用静态 buffer
    req->len = final_len;
    req->target_id = target_id;
    req->opt = opt; 
    
    s_TxQ_Head = (s_TxQ_Head + 1) % TX_PACKET_QUEUE_SIZE;
    s_TxQ_Count++;
    
    OSAL_ExitCritical(ctx);
    
    // 3. 尝试立即触发一次
    _ProcessTxQueue();
    
    return true;
}

bool LoRa_Manager_IsBusy(void) {
    // 如果队列里有数据，也算忙
    return LoRa_Manager_FSM_IsBusy() || (s_TxQ_Count > 0);
}

uint32_t LoRa_Manager_GetSleepDuration(void) {
    // 如果队列有数据，不能休眠
    if (s_TxQ_Count > 0) return 0;
    return LoRa_Manager_FSM_GetNextTimeout();
}
