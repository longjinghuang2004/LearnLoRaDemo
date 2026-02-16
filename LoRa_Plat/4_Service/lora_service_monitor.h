/**
  ******************************************************************************
  * @file    lora_service_monitor.h
  * @author  LoRaPlat Team
  * @brief   LoRaPlat 运行状态监视器 (软件看门狗)
  ******************************************************************************
  */

#ifndef __LORA_SERVICE_MONITOR_H
#define __LORA_SERVICE_MONITOR_H

#include <stdint.h>

/**
 * @brief  初始化监视器
 */
void LoRa_Service_Monitor_Init(void);

/**
 * @brief  运行监视逻辑
 * @note   在 LoRa_Service_Run 中被调用
 */
void LoRa_Service_Monitor_Run(void);

#endif
