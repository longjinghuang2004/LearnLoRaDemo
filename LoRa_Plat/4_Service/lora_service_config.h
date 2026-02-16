/**
  ******************************************************************************
  * @file    lora_service_config.h
  * @author  LoRaPlat Team
  * @brief   LoRa 配置管理接口
  ******************************************************************************
  */

#ifndef __LORA_SERVICE_CONFIG_H
#define __LORA_SERVICE_CONFIG_H

#include "LoRaPlatConfig.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 核心接口
// ============================================================

/**
 * @brief  初始化配置模块
 * @note   优先加载 Flash，若无效则使用默认值
 */
void LoRa_Service_Config_Init(void);

/**
 * @brief  获取当前配置 (只读)
 */
const LoRa_Config_t* LoRa_Service_Config_Get(void);

/**
 * @brief  更新配置并保存到 Flash
 * @param  cfg: 新配置
 */
void LoRa_Service_Config_Set(const LoRa_Config_t *cfg);

/**
 * @brief  恢复出厂设置 (清除 Flash)
 */
void LoRa_Service_Config_FactoryReset(void);

#endif // __LORA_SERVICE_CONFIG_H
