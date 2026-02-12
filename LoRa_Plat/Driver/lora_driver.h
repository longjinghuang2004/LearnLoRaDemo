#ifndef __LORA_DRIVER_H
#define __LORA_DRIVER_H

#include <stdint.h>
#include <stdbool.h>
#include "LoRaPlatConfig.h" // 包含错误码 LoRa_Result_t 和配置结构体

// ============================================================
//                    1. 类型定义
// ============================================================

/**
 * @brief 驱动内部状态 (用于低功耗判断)
 */
typedef enum {
    DRV_STATE_IDLE = 0,     // 空闲 (可休眠)
    DRV_STATE_TX,           // 正在发送 (忙碌)
    DRV_STATE_CONFIG,       // 正在配置 (忙碌)
    DRV_STATE_RESET,        // 正在复位/自愈 (忙碌)
    DRV_STATE_ERROR         // 致命错误 (需人工干预)
} Drv_State_t;

/**
 * @brief 异步事件回调函数原型
 * @param result 操作结果 (LORA_OK 或 错误码)
 */
typedef void (*Drv_Callback_t)(LoRa_Result_t result);

// ============================================================
//                    2. 核心接口 (API)
// ============================================================

/**
 * @brief  初始化 LoRa 驱动
 * @param  cb: 上层(Manager)提供的回调函数，用于接收异步结果
 * @note   初始化后会自动执行一次硬件复位
 */
void Drv_Init(Drv_Callback_t cb);

/**
 * @brief  驱动状态机心跳 (必须在主循环中高频调用)
 * @note   处理超时、状态跳转、AUX检测。无任务时立即返回(Zero-Overhead)。
 */
void Drv_Run(void);

/**
 * @brief  [异步] 发送透传数据
 * @param  data: 数据指针
 * @param  len:  数据长度
 * @return LORA_OK: 请求已受理 (随后会触发回调)
 *         LORA_ERR_BUSY: 驱动忙，拒绝请求
 *         LORA_ERR_PARAM: 参数错误
 */
LoRa_Result_t Drv_AsyncSend(const uint8_t *data, uint16_t len);

/**
 * @brief  [异步] 执行参数配置 (AT指令序列)
 * @param  cfg: 配置结构体
 * @return LORA_OK: 请求已受理
 *         LORA_ERR_BUSY: 驱动忙
 */
LoRa_Result_t Drv_AsyncConfig(const LoRa_Config_t *cfg);

/**
 * @brief  获取当前驱动状态
 * @return 用于主循环判断是否可以进入休眠
 */
Drv_State_t Drv_GetState(void);

#endif // __LORA_DRIVER_H
