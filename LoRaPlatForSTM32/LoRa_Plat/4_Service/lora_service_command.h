/**
  ******************************************************************************
  * @file    lora_service_command.h
  * @author  LoRaPlat Team
  * @brief   LoRa 平台指令解析工具 (V3.4.1 Secure)
  *          提供指令解析服务，但需由用户层显式调用。
  *          格式要求: CMD:<Token>:<Command>=<Params>
  ******************************************************************************
  */

#ifndef __LORA_SERVICE_COMMAND_H
#define __LORA_SERVICE_COMMAND_H

#include <stdint.h>
#include <stdbool.h>

/**
 * @brief  处理平台指令字符串 (带 Token 校验)
 * @param  cmd_str: 指令字符串 (将被 strtok 修改，请传入副本)
 * @return true=指令鉴权通过并已执行, false=鉴权失败或格式错误
 */
bool LoRa_Service_Command_Process(char *cmd_str);

#endif // __LORA_SERVICE_COMMAND_H
