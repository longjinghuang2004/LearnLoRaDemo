/**
  ******************************************************************************
  * @file    lora_service_command.h
  * @author  LoRaPlat Team
  * @brief   LoRa 平台指令解析接口
  ******************************************************************************
  */

#ifndef __LORA_SERVICE_COMMAND_H
#define __LORA_SERVICE_COMMAND_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 核心接口
// ============================================================

/**
 * @brief  处理平台指令字符串
 * @param  cmd_str: 指令字符串 (e.g. "CMD:BIND=...")
 * @return true=指令已处理, false=未知指令或格式错误
 */
bool LoRa_Service_Command_Process(char *cmd_str);

#endif // __LORA_SERVICE_COMMAND_H
