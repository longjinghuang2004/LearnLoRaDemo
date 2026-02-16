/**
  ******************************************************************************
  * @file    lora_at_command_engine.h
  * @author  LoRaPlat Team
  * @brief   AT 指令执行引擎接口
  ******************************************************************************
  */

#ifndef __LORA_AT_COMMAND_ENGINE_H
#define __LORA_AT_COMMAND_ENGINE_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 状态定义
// ============================================================

typedef enum {
    AT_STATUS_OK = 0,       // 执行成功 (收到预期响应)
    AT_STATUS_TIMEOUT,      // 超时 (未收到预期响应)
    AT_STATUS_ERROR,        // 错误 (硬件错误或收到 ERROR)
    AT_STATUS_BUSY          // 忙 (仅异步模式使用)
} AT_Status_t;

// ============================================================
//                    2. 核心接口
// ============================================================

/**
 * @brief  初始化 AT 引擎
 */
void LoRa_AT_Init(void);

/**
 * @brief  执行 AT 指令 (阻塞式)
 * @param  cmd: 指令字符串 (必须以 \r\n 结尾)
 * @param  expect: 期望收到的响应子串 (e.g. "OK")
 * @param  timeout: 超时时间 (ms)
 * @return 执行结果
 */
AT_Status_t LoRa_AT_Execute(const char *cmd, const char *expect, uint32_t timeout);

#endif // __LORA_AT_COMMAND_ENGINE_H
