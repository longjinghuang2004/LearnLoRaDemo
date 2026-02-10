#ifndef __LORA_DRIVER_H
#define __LORA_DRIVER_H

#include "LoRaPlatConfig.h"

// --- 驱动接口定义 ---

// 1. 硬件初始化 (GPIO/UART)
void Drv_Init(void);

// 2. 模式控制
// mode: 0=透传(Comm), 1=配置(Config)
void Drv_SetMode(uint8_t mode);

// 3. 状态查询
// return: true=忙, false=闲
bool Drv_IsBusy(void);

// 4. 数据收发 (透传)
uint16_t Drv_Write(const uint8_t *data, uint16_t len);
uint16_t Drv_Read(uint8_t *buf, uint16_t max_len);

// 5. 参数配置 (阻塞式, 带重试)
// 返回: true=配置成功
bool Drv_SmartConfig(void);

// 6. 辅助: 硬件复位
void Drv_HardReset(void);

#endif
