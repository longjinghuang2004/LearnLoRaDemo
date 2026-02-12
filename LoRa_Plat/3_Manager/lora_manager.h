#ifndef __LORA_MANAGER_H
#define __LORA_MANAGER_H

#include "LoRaPlatConfig.h"
#include <stdint.h>
#include <stdbool.h>

// 缓冲区大小
#define MGR_TX_BUF_SIZE     512
#define MGR_RX_BUF_SIZE     512

// 状态机状态
typedef enum {
    MGR_STATE_IDLE = 0,
    MGR_STATE_ACK_DELAY,    // [新增] 等待发送 ACK
    MGR_STATE_TX_RUNNING,   // 正在发送 (DMA或AUX忙)
    MGR_STATE_WAIT_ACK,     // 等待对方回复 ACK
} MgrState_t;

// 错误码
typedef enum {
    LORA_ERR_NONE = 0,
    LORA_ERR_BUSY,
    LORA_ERR_TX_TIMEOUT,
    LORA_ERR_ACK_TIMEOUT,
    LORA_ERR_CRC_FAIL,
    LORA_ERR_MEM_OVERFLOW
} LoRaError_t;

// 回调函数原型
typedef void (*OnRxData_t)(uint8_t *data, uint16_t len, uint16_t src_id);
typedef void (*OnTxResult_t)(bool success);
typedef void (*OnError_t)(LoRaError_t err);

typedef struct {
    uint8_t TxBuffer[MGR_TX_BUF_SIZE];
    uint8_t RxBuffer[MGR_RX_BUF_SIZE];
    
    MgrState_t state;
    uint32_t   state_tick;  // 状态进入时间戳
    uint8_t    tx_seq;
    uint8_t    retry_cnt;
    uint16_t   rx_len;
    
    // 待发送 ACK 信息 (用于 ACK_DELAY 状态)
    struct {
        uint16_t target_id;
        uint8_t  seq;
        bool     pending;
    } ack_pending;
    
    // 身份信息
    uint16_t   local_id;
    uint16_t   group_id;
    uint32_t   uuid;        // <--- [关键] 必须包含此字段
    
    OnRxData_t      cb_on_rx;
    OnTxResult_t    cb_on_tx;
    OnError_t       cb_on_err;
		
		
    bool     is_sending_ack; // [新增] 标记当前是否正在发送 ACK
} LoRa_Manager_t;

extern LoRa_Manager_t g_LoRaManager;

// 接口
void Manager_Init(OnRxData_t on_rx, OnTxResult_t on_tx, OnError_t on_err);
bool Manager_SendPacket(const uint8_t *payload, uint16_t len, uint16_t target_id);
void Manager_Run(void);

#endif
