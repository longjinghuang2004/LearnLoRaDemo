#ifndef __LORA_SERVICE_H
#define __LORA_SERVICE_H

#include "lora_osal.h"
#include "LoRaPlatConfig.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 数据结构定义
// ============================================================

typedef struct {
    int16_t rssi; 
    int8_t  snr;  
} LoRa_RxMeta_t;

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
//                    2. 回调接口
// ============================================================
typedef struct {
    void (*SaveConfig)(const LoRa_Config_t *cfg);
    void (*LoadConfig)(LoRa_Config_t *cfg);
    uint32_t (*GetRandomSeed)(void); 
    void (*SystemReset)(void);       
    void (*OnRecvData)(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta);
    void (*OnEvent)(LoRa_Event_t event, void *arg);
} LoRa_Callback_t;

// ============================================================
//                    3. 全局函数
// ============================================================

void LoRa_Service_Init(const LoRa_Callback_t *callbacks, uint16_t override_net_id);
void LoRa_Service_Run(void);
bool LoRa_Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id);
void LoRa_Service_FactoryReset(void);

// 配置访问
const LoRa_Config_t* LoRa_Service_GetConfig(void);
void LoRa_Service_SetConfig(const LoRa_Config_t *cfg);

// [新增] 内部通知接口 (供 Command 模块调用)
void LoRa_Service_NotifyEvent(LoRa_Event_t event, void *arg);

#endif // __LORA_SERVICE_H
