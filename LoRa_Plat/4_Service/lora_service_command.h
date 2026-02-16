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
 * @param  out_resp: [输出] 响应字符串缓冲区
 * @param  max_len:  [输入] 缓冲区最大长度
 * @return true=执行成功且需要发送回执, false=忽略
 */
bool LoRa_Service_Command_Process(char *cmd_str, char *out_resp, uint16_t max_len);

#endif // __LORA_SERVICE_COMMAND_H
