#ifndef __LORA_MANAGER_H
#define __LORA_MANAGER_H

#include "LoRaPlatConfig.h"
#include <stdint.h>
#include <stdbool.h>

// 回调定义
typedef void (*LoRa_OnRxData_t)(uint8_t *data, uint16_t len, uint16_t src_id);

// 接口
void LoRa_Manager_Init(LoRa_OnRxData_t on_rx);
void LoRa_Manager_Run(void);
bool LoRa_Manager_Send(const uint8_t *payload, uint16_t len, uint16_t target_id);
bool LoRa_Manager_IsBusy(void);

#endif
