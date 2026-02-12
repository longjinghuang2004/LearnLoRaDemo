#ifndef __LORA_DRIVER_H
#define __LORA_DRIVER_H

#include "LoRaPlatConfig.h" // 包含基础定义

// ============================================================
//                    1. 类型定义
// ============================================================
typedef enum {
    DRV_STATE_IDLE = 0,     ///< 空闲态
    DRV_STATE_TX_RUNNING,   ///< 正在发送 (物理层忙)
    DRV_STATE_TX_RECOVERY,  ///< 发送后冷却
    DRV_STATE_AT_PROCESS,   ///< 正在执行 AT 指令序列
} Drv_State_t;

typedef struct {
    const char *cmd;        ///< 发送指令
    const char *expect;     ///< 期望回复
    uint16_t    wait_ms;    ///< 额外延时
} AT_Job_t;

typedef void (*Drv_Callback_t)(LoRa_Result_t result);

// ============================================================
//                    2. 核心接口
// ============================================================
void          Drv_Init(Drv_Callback_t cb);
void          Drv_Run(void);
LoRa_Result_t Drv_AsyncSend(const uint8_t *data, uint16_t len);
LoRa_Result_t Drv_AsyncConfig(void);
Drv_State_t   Drv_GetState(void);
bool          Drv_IsIdle(void); // 确保有这个声明

// 阻塞式辅助函数 (用于初始化/恢复出厂)
bool Drv_ApplyConfig(const LoRa_Config_t *cfg);
bool Drv_SmartConfig(void);

#endif
