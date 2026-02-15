#include "lora_service.h"
#include "lora_service_config.h"
// [移除] #include "lora_service_command.h" 
#include "lora_manager.h"
#include "lora_driver.h" 
#include "lora_port.h" 
#include "lora_osal.h"
#include <string.h>
#include <stdlib.h>



static LoRa_Callback_t g_cb; 
#define CMD_PREFIX "CMD:"

// [新增] 定义无穷大时间 (表示无定时任务)
#define LORA_TIMEOUT_INFINITE   0xFFFFFFFF

// ============================================================
//                    内部回调适配
// ============================================================

static void _On_Mgr_RxData(uint8_t *data, uint16_t len, uint16_t src_id) {
    // [修改] 彻底移除 CMD 拦截逻辑
    // 平台不再关心数据内容，直接透传给业务层
    // 安全性提升：防止恶意指令绕过业务层直接重置设备
    if (g_cb.OnRecvData) {
        LoRa_RxMeta_t meta = { .rssi = -128, .snr = 0 };
        g_cb.OnRecvData(src_id, data, len, &meta);
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
    LoRa_Manager_Init(LoRa_Service_Config_Get(), _On_Mgr_RxData);
    
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
    LORA_CHECK(data && len > 0, false);
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
    LORA_CHECK_VOID(cfg);
    LoRa_Service_Config_Set(cfg);
    if (g_cb.SaveConfig) g_cb.SaveConfig(cfg);
}

void LoRa_Service_NotifyEvent(LoRa_Event_t event, void *arg) {
    if (g_cb.OnEvent) g_cb.OnEvent(event, arg);
}

// [新增] 综合计算休眠时间
uint32_t LoRa_Service_GetSleepDuration(void) {
    // 1. 硬件层一票否决
    if (LoRa_Driver_IsBusy()) {
        return 0; 
    }

    // 2. 逻辑层计算剩余时间
    uint32_t logic_sleep = LoRa_Manager_GetSleepDuration();
    
    // 3. 安全余量扣除 (Safety Margin)
    // 唤醒和恢复时钟需要时间，建议扣除 2ms
    if (logic_sleep > 2 && logic_sleep != LORA_TIMEOUT_INFINITE) {
        return logic_sleep - 2;
    }
    
    return logic_sleep;
}
