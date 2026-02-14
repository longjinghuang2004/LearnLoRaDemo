#ifndef __LORA_MANAGER_H
#define __LORA_MANAGER_H

#include "LoRaPlatConfig.h"
#include <stdint.h>
#include <stdbool.h>

// 缓冲区大小
#define MGR_TX_BUF_SIZE     512
#define MGR_RX_BUF_SIZE     512

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

// [移除] extern LoRa_Manager_t g_LoRaManager;

// 接口
void Manager_Init(OnRxData_t on_rx, OnTxResult_t on_tx, OnError_t on_err);

// [新增] 设置身份信息 (替代直接访问结构体)
void Manager_SetIdentity(uint16_t local_id, uint16_t group_id, uint32_t uuid);

bool Manager_SendPacket(const uint8_t *payload, uint16_t len, uint16_t target_id);
void Manager_Run(void);

// [新增] 状态查询
bool Manager_IsBusy(void);

#endif
