#ifndef __LORA_MANAGER_H
#define __LORA_MANAGER_H

#include "LoRaPlatConfig.h" // [关键] 引入统一配置
#include <stdint.h>
#include <stdbool.h>

// 缓冲区大小 
#define MGR_TX_BUF_SIZE     512
#define MGR_RX_BUF_SIZE     512

// 协议常量 
#define PROTOCOL_HEAD_0     'C'
#define PROTOCOL_HEAD_1     'M'
#define PROTOCOL_TAIL_0     '\r'
#define PROTOCOL_TAIL_1     '\n'

// 控制字掩码 
#define CTRL_MASK_TYPE      0x80 
#define CTRL_MASK_NEED_ACK  0x40 
#define CTRL_MASK_HAS_CRC   0x20 

// 状态机状态
typedef enum {
    MGR_STATE_IDLE = 0,
    MGR_STATE_TX_SENDING, // 正在等待驱动层发送完成
    MGR_STATE_WAIT_ACK,   // 正在等待对方 ACK
} MgrState_t;

// 回调函数原型
typedef void (*OnRxData_t)(uint8_t *data, uint16_t len, uint16_t src_id);
typedef void (*OnTxResult_t)(bool success);
typedef void (*OnError_t)(LoRa_Result_t err); // [修改] 使用统一错误码

typedef struct {
    uint8_t TxBuffer[MGR_TX_BUF_SIZE];
    uint8_t RxBuffer[MGR_RX_BUF_SIZE];
    
    MgrState_t state;
    uint32_t   state_tick;
    uint8_t    tx_seq;
    uint8_t    retry_cnt;
    uint16_t   rx_len;
    
    // 身份信息
    uint16_t   local_id;    // 当前逻辑 ID (NetID)
    uint16_t   group_id;    // 组 ID
    uint32_t   uuid;        // 唯一标识
    
    OnRxData_t      cb_on_rx;
    OnTxResult_t    cb_on_tx;
    OnError_t       cb_on_err;
} LoRa_Manager_t;

extern LoRa_Manager_t g_LoRaManager;

// 接口
void Manager_Init(OnRxData_t on_rx, OnTxResult_t on_tx, OnError_t on_err);
bool Manager_SendPacket(const uint8_t *payload, uint16_t len, uint16_t target_id);
void Manager_Run(void);

#endif
