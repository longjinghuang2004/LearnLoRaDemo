/**
  ******************************************************************************
  * @file    lora_driver.h
  * @author  LoRaPlat Team
  * @brief   Layer 2: 通用异步驱动层 (Universal Asynchronous Driver)
  ******************************************************************************
  */

#ifndef __LORA_DRIVER_H
#define __LORA_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h> // for NULL
#include "lora_port.h"

// ============================================================
//                    1. 类型定义
// ============================================================

/**
 * @brief 驱动操作结果码
 */
typedef enum {
    LORA_OK = 0,            ///< 操作成功 / 请求已受理
    LORA_ERR_BUSY,          ///< 驱动忙
    LORA_ERR_PARAM,         ///< 参数错误
    LORA_ERR_HARDWARE,      ///< 硬件故障 (如 AUX 永久拉高)
    LORA_ERR_TIMEOUT,       ///< 操作超时
    LORA_ERR_AT_FAIL        ///< AT 指令执行失败
} LoRa_Result_t;

/**
 * @brief 驱动内部状态机
 */
typedef enum {
    DRV_STATE_IDLE = 0,     ///< 空闲态
    DRV_STATE_TX_RUNNING,   ///< 正在发送 (物理层忙)
    DRV_STATE_TX_RECOVERY,  ///< 发送后冷却
    DRV_STATE_AT_PROCESS,   ///< 正在执行 AT 指令序列
} Drv_State_t;

/**
 * @brief AT 指令任务结构体
 * @note  用于定义配置流程
 */
typedef struct {
    const char *cmd;        ///< 发送指令 (字符串)
    const char *expect;     ///< 期望回复 (字符串, NULL表示不检查)
    uint16_t    wait_ms;    ///< 指令执行后的额外延时
} AT_Job_t;

/**
 * @brief 异步事件回调函数原型
 */
typedef void (*Drv_Callback_t)(LoRa_Result_t result);

// ============================================================
//                    2. 外部引用 (Config)
// ============================================================

// 引用外部定义的配置任务表 (在 lora_driver_config.c 中定义)
extern const AT_Job_t g_LoRaConfigJobs[];

// ============================================================
//                    3. 核心接口 (API)
// ============================================================

void          Drv_Init(Drv_Callback_t cb);
void          Drv_Run(void);
LoRa_Result_t Drv_AsyncSend(const uint8_t *data, uint16_t len);
LoRa_Result_t Drv_AsyncConfig(void);
Drv_State_t   Drv_GetState(void);

#endif // __LORA_DRIVER_H
