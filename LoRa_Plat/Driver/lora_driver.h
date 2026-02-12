#ifndef __LORA_DRIVER_H
#define __LORA_DRIVER_H

#include "LoRaPlatConfig.h"
#include <stdbool.h>
#include <stdint.h>

// ============================================================
//                    1. 核心接口 (Core)
// ============================================================

/**
 * @brief  驱动初始化 (阻塞式)
 * @note   包含握手、救砖、参数同步。耗时约 1-3 秒。
 * @return true=成功, false=硬件故障
 */
bool Drv_Init(const LoRa_Config_t *cfg);

/**
 * @brief  异步发送数据 (非阻塞)
 * @param  data: 数据指针
 * @param  len:  长度
 * @return true=已启动DMA, false=忙或错误
 */
bool Drv_AsyncSend(const uint8_t *data, uint16_t len);

/**
 * @brief  读取接收数据
 */
uint16_t Drv_Read(uint8_t *buf, uint16_t max_len);

/**
 * @brief  查询驱动是否忙碌
 */
bool Drv_IsBusy(void);

// ============================================================
//                    2. 配置接口 (Config)
// ============================================================

// 获取 AT 指令字符串 (由 lora_driver_config.c 实现)
const char* Drv_GetAtCmd_Reset(void);
const char* Drv_GetAtCmd_Mode(uint8_t mode);
void Drv_GetAtCmd_Rate(uint8_t channel, uint8_t rate, char *buf);
void Drv_GetAtCmd_Addr(uint16_t addr, char *buf);
void Drv_GetAtCmd_Power(uint8_t power, char *buf);

#endif // __LORA_DRIVER_H
