#ifndef __LORA_SERVICE_H
#define __LORA_SERVICE_H

#include "LoRaPlatConfig.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 数据结构定义
// ============================================================

// 接收元数据
typedef struct {
    int16_t rssi; 
    int8_t  snr;  
} LoRa_RxMeta_t;

// 事件定义 (修复：补全所有事件)
typedef enum {
    LORA_EVENT_INIT_SUCCESS = 0,
    LORA_EVENT_BIND_SUCCESS,    
    LORA_EVENT_GROUP_UPDATE,    
    LORA_EVENT_CONFIG_START,    
    LORA_EVENT_CONFIG_COMMIT,   
    LORA_EVENT_FACTORY_RESET,   
    LORA_EVENT_REBOOT_REQ,      
    LORA_EVENT_MSG_RECEIVED,    
    LORA_EVENT_MSG_SENT         
} LoRa_Event_t;

// ============================================================
//                    2. 抽象接口定义 (回调函数)
// ============================================================
typedef struct {
    // --- 存储接口 ---
    void (*SaveConfig)(const LoRa_Config_t *cfg);
    void (*LoadConfig)(LoRa_Config_t *cfg);
    
    // --- 硬件能力接口 ---
    uint32_t (*GetTick)(void);
    uint32_t (*GetRandomSeed)(void);
    void (*SystemReset)(void);
    
    // --- 业务接口 ---
    void (*OnRecvData)(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta);
    
    // 系统事件通知
    void (*OnEvent)(LoRa_Event_t event, void *arg);
    
} LoRa_Callback_t;

// ============================================================
//                    3. 全局变量与函数
// ============================================================

extern LoRa_Config_t g_LoRaConfig_Current;

void LoRa_Service_Init(const LoRa_Callback_t *callbacks, uint16_t override_net_id);
void LoRa_Service_Run(void);
bool LoRa_Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id);
void LoRa_Service_FactoryReset(void);

// [修复] 暴露此接口供 Manager 和 Main 使用
const LoRa_Config_t* Service_GetConfig(void);

#endif
