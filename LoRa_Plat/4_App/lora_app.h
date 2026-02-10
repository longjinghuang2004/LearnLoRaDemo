#ifndef __LORA_APP_H
#define __LORA_APP_H

#include <stdint.h>

/* ========================================================================== */
/*                                 业务接口                                    */
/* ========================================================================== */

/**
 * @brief 业务初始化 (注册回调，配置默认参数)
 */
void LoRa_App_Init(void);

/**
 * @brief 业务主循环 (包含管理器轮询)
 */
void LoRa_App_Task(void);

/**
 * @brief 发送业务指令 (示例)
 * @param cmd_str 指令字符串 (如 "LED_ON")
 */
void LoRa_App_SendCmd(const char *cmd_str);

#endif // __LORA_APP_H

