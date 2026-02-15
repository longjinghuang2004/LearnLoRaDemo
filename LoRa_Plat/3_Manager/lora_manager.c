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
static const LoRa_Cipher_t *s_Cipher = NULL; // [新增] 加密接口指针

// ============================================================
//                    核心实现
// ============================================================

void LoRa_Manager_Init(const LoRa_Config_t *cfg, LoRa_OnRxData_t on_rx) {
    LORA_CHECK_VOID(cfg); 
    s_Mgr_Config = cfg;
    s_OnRx = on_rx;
    s_Cipher = NULL; // 默认无加密
    
    LoRa_Manager_Buffer_Init();
    LoRa_Manager_FSM_Init(cfg); 
}

void LoRa_Manager_RegisterCipher(const LoRa_Cipher_t *cipher) {
    s_Cipher = cipher;
}

void LoRa_Manager_Run(void) {
    // 1. 从 Port 拉取数据
    LoRa_Manager_Buffer_PullFromPort();
    
    // 2. 解析数据包
    LoRa_Packet_t pkt;
    memset(&pkt, 0, sizeof(pkt));
    
    if (s_Mgr_Config && LoRa_Manager_Buffer_GetRxPacket(&pkt, s_Mgr_Config->net_id, s_Mgr_Config->group_id, 
                                        s_RxWorkspace, RX_WORKSPACE_SIZE)) {
        
        // 3. 交给 FSM 处理 (ACK 逻辑 + 去重)
        // 注意：FSM 处理的是协议头 (Seq, SourceID)，此时 Payload 即使是密文也不影响去重
        bool valid_new_packet = LoRa_Manager_FSM_ProcessRxPacket(&pkt);
        
        // 4. 如果是有效新数据包，回调上层
        if (valid_new_packet && s_OnRx) {
            // [新增] 解密处理
            if (s_Cipher && s_Cipher->Decrypt && pkt.PayloadLen > 0) {
                // 原地解密 (In-place decryption)
                // 注意：这里假设解密后长度不会增加。如果算法特殊，需使用临时 buffer
                uint16_t new_len = s_Cipher->Decrypt(pkt.Payload, pkt.PayloadLen, pkt.Payload);
                pkt.PayloadLen = new_len;
            }
            
            s_OnRx(pkt.Payload, pkt.PayloadLen, pkt.SourceID);
        }
    }
    
    // 5. 运行状态机
    LoRa_Manager_FSM_Run(s_RxWorkspace, RX_WORKSPACE_SIZE);
}

bool LoRa_Manager_Send(const uint8_t *payload, uint16_t len, uint16_t target_id) {
    uint8_t tx_stack_buf[LORA_MAX_PAYLOAD_LEN + 32]; 
    
    // [新增] 加密处理
    // 为了不修改 const payload，我们需要一个临时缓冲区存放加密后的数据
    uint8_t final_payload[LORA_MAX_PAYLOAD_LEN];
    uint16_t final_len = len;
    
    if (len > LORA_MAX_PAYLOAD_LEN) return false;

    if (s_Cipher && s_Cipher->Encrypt) {
        final_len = s_Cipher->Encrypt(payload, len, final_payload);
        if (final_len > LORA_MAX_PAYLOAD_LEN) return false; // 加密后超长
    } else {
        memcpy(final_payload, payload, len);
    }

    // 将处理后的数据交给 FSM
    return LoRa_Manager_FSM_Send(final_payload, final_len, target_id, tx_stack_buf, sizeof(tx_stack_buf));
}

bool LoRa_Manager_IsBusy(void) {
    return LoRa_Manager_FSM_IsBusy();
}

uint32_t LoRa_Manager_GetSleepDuration(void) {
    return LoRa_Manager_FSM_GetNextTimeout();
}
