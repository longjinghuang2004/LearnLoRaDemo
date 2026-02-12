#ifndef __LORA_SERVICE_H
#define __LORA_SERVICE_H

#include "LoRaPlatConfig.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 数据结构定义
// ============================================================

// 接收元数据 (为未来组网预留)
typedef struct {
    int16_t rssi; // 接收信号强度 (dBm), -128表示无效
    int8_t  snr;  // 信噪比 (dB), 0表示无效
} LoRa_RxMeta_t;

// 事件定义 (与 lora_service.c 中的使用匹配)
typedef enum {
    LORA_EVENT_INIT_SUCCESS = 0,
    LORA_EVENT_BIND_SUCCESS,    // 绑定新ID成功
    LORA_EVENT_GROUP_UPDATE,    // 组ID更新
    LORA_EVENT_CONFIG_START,    // 进入配置模式
    LORA_EVENT_CONFIG_COMMIT,   // 配置提交
    LORA_EVENT_FACTORY_RESET,   // 恢复出厂
    LORA_EVENT_REBOOT_REQ,      // 请求重启
    LORA_EVENT_MSG_RECEIVED,    // 收到任意消息
    LORA_EVENT_MSG_SENT         // 发送完成
} LoRa_Event_t;

// ============================================================
//                    2. 抽象接口定义 (回调函数)
// ============================================================
typedef struct {
    // --- 存储接口 (必须实现) ---
    void (*SaveConfig)(const LoRa_Config_t *cfg);
    void (*LoadConfig)(LoRa_Config_t *cfg);
    
    // --- 硬件能力接口 (必须实现) ---
    uint32_t (*GetTick)(void);
    uint32_t (*GetRandomSeed)(void);
    void (*SystemReset)(void);
    
    // --- 业务接口 (可选) ---
    void (*OnRecvData)(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta);
    
    // 系统事件通知
    void (*OnEvent)(LoRa_Event_t event, void *arg);
    
} LoRa_Callback_t;

// ============================================================
//                    3. 全局变量与函数
// ============================================================

extern LoRa_Config_t g_LoRaConfig_Current;

/**
  * @brief  初始化 LoRa 服务层
  */
void LoRa_Service_Init(const LoRa_Callback_t *callbacks, uint16_t override_net_id);

/**
  * @brief  服务层主循环
  */
void LoRa_Service_Run(void);

/**
  * @brief  发送数据
  */
bool LoRa_Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id);

/**
  * @brief  执行工厂重置
  */
void LoRa_Service_FactoryReset(void);

// [新增] 获取当前配置指针
const LoRa_Config_t* LoRa_Service_GetConfig(void);
#endif // __LORA_SERVICE_H
