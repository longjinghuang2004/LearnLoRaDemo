#ifndef __LORA_MANAGER_H
#define __LORA_MANAGER_H

#include "LoRaPlatConfig.h"

// 缓冲区大小
#define MGR_TX_BUF_SIZE     256
#define MGR_RX_BUF_SIZE     256

// 状态机状态
typedef enum {
    MGR_STATE_IDLE = 0,
    MGR_STATE_TX_SENDING,   // 正在发送 (无需ACK)
    MGR_STATE_WAIT_ACK,     // 发送完毕，等待ACK
} MgrState_t;

// 回调函数原型
typedef void (*OnMgrRecv_t)(uint8_t *data, uint16_t len, uint16_t src_id, int16_t rssi);
typedef void (*OnMgrTxResult_t)(bool success);
typedef void (*OnMgrError_t)(LoRa_Result_t err);

// 管理器对象
typedef struct {
    // --- 缓冲区 ---
    uint8_t tx_buf[MGR_TX_BUF_SIZE];
    uint16_t tx_len;
    
    uint8_t rx_buf[MGR_RX_BUF_SIZE];
    uint16_t rx_len;
    
    // --- 身份 ---
    uint16_t local_id;
    uint16_t group_id;
    
    // --- 发送状态机 ---
    MgrState_t state;
    uint32_t   state_tick;
    uint8_t    tx_seq;
    uint8_t    retry_cnt;
    
    // --- ACK 挂起状态机 (V3.0 核心修复) ---
    bool      ack_pending;       // 是否有待发送的 ACK
    uint32_t  ack_timestamp;     // 收到数据的时间戳
    uint16_t  ack_target_id;     // 待回复的目标 ID
    uint8_t   ack_seq;           // 待回复的序列号
    
    // --- 回调 ---
    OnMgrRecv_t     cb_on_rx;
    OnMgrTxResult_t cb_on_tx;
    OnMgrError_t    cb_on_err;
    
} LoRa_Manager_t;

extern LoRa_Manager_t g_LoRaManager;

// 接口
void Manager_Init(uint16_t local_id, uint16_t group_id, 
                  OnMgrRecv_t on_rx, OnMgrTxResult_t on_tx, OnMgrError_t on_err);

LoRa_Result_t Manager_Send(const uint8_t *payload, uint16_t len, uint16_t target_id, bool need_ack);

void Manager_Run(void);
void Manager_RxCallback(uint8_t byte);

#endif
