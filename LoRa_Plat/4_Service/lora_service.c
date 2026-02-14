#include "lora_service.h"
#include "lora_service_config.h"
#include "lora_service_command.h"
#include "lora_manager.h"
#include "lora_driver.h" 
#include "lora_port.h" 
#include "lora_osal.h"
#include <string.h>
#include <stdlib.h>

static LoRa_Callback_t g_cb; 
#define CMD_PREFIX "CMD:"

// ============================================================
//                    内部回调适配
// ============================================================

static void _On_Mgr_RxData(uint8_t *data, uint16_t len, uint16_t src_id) {
    // 1. 检查是否为平台指令
    if (len > strlen(CMD_PREFIX) && strncmp((char*)data, CMD_PREFIX, strlen(CMD_PREFIX)) == 0) {
        LoRa_Service_Command_Process((char*)(data + strlen(CMD_PREFIX)));
    } 
    // 2. 否则为业务数据
    else {
        if (g_cb.OnRecvData) {
            LoRa_RxMeta_t meta = { .rssi = -128, .snr = 0 };
            g_cb.OnRecvData(src_id, data, len, &meta);
        }
    }
}

// ============================================================
//                    核心接口实现
// ============================================================

void LoRa_Service_Init(const LoRa_Callback_t *callbacks, uint16_t override_net_id) {
    if (callbacks) g_cb = *callbacks;
    
    // 1. 初始化配置模块
    LoRa_Service_Config_Init();
    
    // 2. 加载 Flash 配置 (如果有)
    if (g_cb.LoadConfig) {
        LoRa_Config_t flash_cfg;
        g_cb.LoadConfig(&flash_cfg);
        if (flash_cfg.magic == LORA_CFG_MAGIC) {
            LoRa_Service_Config_Set(&flash_cfg);
        } else {
            // Flash 无效，保存默认值
            if (g_cb.SaveConfig) g_cb.SaveConfig(LoRa_Service_Config_Get());
        }
    }
    
    // 3. 调试覆盖
    if (override_net_id != 0) {
        LoRa_Config_t temp = *LoRa_Service_Config_Get();
        temp.net_id = override_net_id;
        LoRa_Service_Config_Set(&temp);
    }
    
    // 4. 初始化 Manager
    LoRa_Manager_Init(_On_Mgr_RxData);
    
    // 5. 初始化 Driver (阻塞)
    // 注意：这里依然调用旧的 Drv_Init，Step 4 会替换它
    if (LoRa_Driver_Init(LoRa_Service_Config_Get())) {
        LoRa_Service_NotifyEvent(LORA_EVENT_INIT_SUCCESS, NULL);
    } else {
        while(1); // 死循环报警
    }
}

void LoRa_Service_Run(void) {
    LoRa_Manager_Run();
}

bool LoRa_Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id) {
    return LoRa_Manager_Send(data, len, target_id);
}

void LoRa_Service_FactoryReset(void) {
    LoRa_Service_Config_FactoryReset();
    if (g_cb.SaveConfig) g_cb.SaveConfig(LoRa_Service_Config_Get());
    LoRa_Service_NotifyEvent(LORA_EVENT_FACTORY_RESET, NULL);
    if (g_cb.SystemReset) g_cb.SystemReset();
}

const LoRa_Config_t* LoRa_Service_GetConfig(void) {
    return LoRa_Service_Config_Get();
}

void LoRa_Service_SetConfig(const LoRa_Config_t *cfg) {
    LoRa_Service_Config_Set(cfg);
    if (g_cb.SaveConfig) g_cb.SaveConfig(cfg);
}

void LoRa_Service_NotifyEvent(LoRa_Event_t event, void *arg) {
    if (g_cb.OnEvent) g_cb.OnEvent(event, arg);
}
