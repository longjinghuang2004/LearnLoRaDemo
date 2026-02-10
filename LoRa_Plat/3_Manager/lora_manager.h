#ifndef __LORA_MANAGER_H
#define __LORA_MANAGER_H

#include "mod_lora.h" // 需要引用 Layer 2 的类型定义

/* ========================================================================== */
/*                                 设备 ID 定义                                */
/* ========================================================================== */

typedef enum {
    LORA_DEV_A = 0, // 主模块
    LORA_DEV_B = 1, // 备用模块 (预留)
    LORA_DEV_MAX
} LoRa_DevID_t;

/* ========================================================================== */
/*                                 管理接口                                    */
/* ========================================================================== */

/**
 * @brief 初始化管理器 (实例化对象并绑定硬件)
 */
void LoRa_Mgr_Init(void);

/**
 * @brief 轮询任务 (需在主循环中周期性调用)
 */
void LoRa_Mgr_Task(void);

/**
 * @brief 注册业务回调
 * @param id 设备ID
 * @param cb 回调函数
 */
void LoRa_Mgr_RegisterCallback(LoRa_DevID_t id, LoRa_RxHandler_t cb);

/**
 * @brief 发送数据
 * @param id 设备ID
 * @param data 数据指针
 * @param len 数据长度
 */
void LoRa_Mgr_Send(LoRa_DevID_t id, const uint8_t *data, uint16_t len);

/* ========================================================================== */
/*                                 配置接口                                    */
/* ========================================================================== */

// 设置信道 (0-31)
void LoRa_Mgr_SetChannel(LoRa_DevID_t id, uint8_t channel);

// 设置功率
void LoRa_Mgr_SetPower(LoRa_DevID_t id, LoRa_Power_e power);

// 设置协议包头包尾 (运行时修改)
void LoRa_Mgr_SetProtocol(LoRa_DevID_t id, uint8_t h0, uint8_t h1, uint8_t t0, uint8_t t1);

#endif // __LORA_MANAGER_H
