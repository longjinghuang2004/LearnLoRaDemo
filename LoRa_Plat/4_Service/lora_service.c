#include "lora_service.h"
#include "lora_manager.h"
#include "lora_driver.h" 
#include "lora_port.h" 
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// 全局配置
LoRa_Config_t g_LoRaConfig_Current; 
static LoRa_Callback_t g_cb; 

// 内部函数声明
static void _Svc_NotifyEvent(LoRa_Event_t event, void *arg);

// Manager 回调适配
static void _On_Mgr_RxData(uint8_t *data, uint16_t len, uint16_t src_id) {
    // 简单透传给上层
    if (g_cb.OnRecvData) {
        LoRa_RxMeta_t meta;
        meta.rssi = -128; 
        meta.snr = 0;
        g_cb.OnRecvData(src_id, data, len, &meta);
    }
    _Svc_NotifyEvent(LORA_EVENT_MSG_RECEIVED, data);
}

static void _Svc_NotifyEvent(LoRa_Event_t event, void *arg) {
    if (g_cb.OnEvent) g_cb.OnEvent(event, arg);
}

// 接口实现
void LoRa_Service_Init(const LoRa_Callback_t *callbacks, uint16_t override_net_id) {
    if (callbacks) g_cb = *callbacks;
    
    // 1. 读取配置
    if (g_cb.LoadConfig) g_cb.LoadConfig(&g_LoRaConfig_Current);
    
    // 2. 首次初始化 (Magic Check)
    if (g_LoRaConfig_Current.magic != LORA_CFG_MAGIC) {
        if (g_cb.GetRandomSeed) srand(g_cb.GetRandomSeed());
        g_LoRaConfig_Current.uuid = ((uint32_t)rand() << 16) | rand();
        g_LoRaConfig_Current.magic = LORA_CFG_MAGIC;
        g_LoRaConfig_Current.net_id = LORA_ID_UNASSIGNED;
        g_LoRaConfig_Current.group_id = LORA_GROUP_ID_DEFAULT;
        g_LoRaConfig_Current.hw_addr = LORA_HW_ADDR_DEFAULT;
        g_LoRaConfig_Current.channel = DEFAULT_LORA_CHANNEL;
        g_LoRaConfig_Current.power = DEFAULT_LORA_POWER;
        g_LoRaConfig_Current.air_rate = DEFAULT_LORA_RATE;
        g_LoRaConfig_Current.tmode = DEFAULT_LORA_TMODE;
        
        if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Current);
    }
    
    if (override_net_id != 0) g_LoRaConfig_Current.net_id = override_net_id;
    
    // 3. 初始化 Manager (会自动初始化 Driver)
    g_LoRaManager.local_id = g_LoRaConfig_Current.net_id;
    g_LoRaManager.group_id = g_LoRaConfig_Current.group_id;
    g_LoRaManager.uuid     = g_LoRaConfig_Current.uuid;
    
    Manager_Init(_On_Mgr_RxData, NULL, NULL); 
    
    // 注意：Phase 1 暂时不自动执行 Drv_AsyncConfig
    // 默认使用模块当前的配置，或者依赖 Drv_Init 中的复位
    
    _Svc_NotifyEvent(LORA_EVENT_INIT_SUCCESS, NULL);
}

void LoRa_Service_Run(void) {
    Manager_Run();
}

bool LoRa_Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id) {
    bool res = Manager_SendPacket(data, len, target_id);
    if (res) _Svc_NotifyEvent(LORA_EVENT_MSG_SENT, NULL);
    return res;
}

// 暴露给主循环查询状态
bool LoRa_Service_IsIdle(void) {
    return (Drv_GetState() == DRV_STATE_IDLE);
}
