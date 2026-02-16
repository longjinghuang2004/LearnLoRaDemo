#ifndef __LORA_DRIVER_H
#define __LORA_DRIVER_H

#include "LoRaPlatConfig.h"
#include <stdbool.h>
#include <stdint.h>

/**
 * @brief  驱动初始化 (阻塞式)
 */
bool LoRa_Driver_Init(const LoRa_Config_t *cfg);

/**
 * @brief  异步发送数据 (非阻塞)
 * @return true=已启动DMA, false=忙或错误
 */
bool LoRa_Driver_AsyncSend(const uint8_t *data, uint16_t len);

/**
 * @brief  读取接收数据
 */
uint16_t LoRa_Driver_Read(uint8_t *buf, uint16_t max_len);

/**
 * @brief  查询驱动是否忙碌
 */
bool LoRa_Driver_IsBusy(void);

#endif // __LORA_DRIVER_H
