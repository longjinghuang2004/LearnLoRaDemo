/**
  ******************************************************************************
  * @file    lora_service.c
  * @author  LoRaPlat Team
  * @brief   LoRa 业务服务层实现 (V3.9)
  *          负责协调 Manager, Config, Monitor 以及对外接口
  ******************************************************************************
  */

#include "lora_service.h"
#include "lora_manager.h"
#include "lora_service_config.h"
#include "lora_service_monitor.h"
#include "lora_driver.h"
#include "lora_osal.h"
#include <string.h>

// ============================================================
//                    内部变量
// ============================================================

static const LoRa_Callback_t *s_AppCb = NULL;

// ============================================================
//                    内部回调 (Manager -> Service)
// ============================================================

// 接收数据回调
static void _Service_OnRecv(uint8_t *data, uint16_t len, uint16_t src_id) {
    if (s_AppCb && s_AppCb->OnRecvData) {
        // 简单的 RSSI 模拟 (实际应从驱动获取，这里暂未实现硬件读取 RSSI)
        LoRa_RxMeta_t meta = { .rssi = -60, .snr = 10 };
        s_AppCb->OnRecvData(src_id, data, len, &meta);
    }
    
    // 触发事件
    if (s_AppCb && s_AppCb->OnEvent) {
        s_AppCb->OnEvent(LORA_EVENT_MSG_RECEIVED, NULL);
    }
}

// ============================================================
//                    核心接口实现
// ============================================================

void LoRa_Service_Init(const LoRa_Callback_t *callbacks, uint16_t override_net_id) {
    s_AppCb = callbacks;
    
    // 1. 初始化配置模块
    LoRa_Service_Config_Init();
    
    // [可选] 覆盖 NetID (用于调试或动态分配)
    if (override_net_id != 0) {
        LoRa_Config_t temp_cfg = *LoRa_Service_Config_Get();
        temp_cfg.net_id = override_net_id;
        LoRa_Service_Config_Set(&temp_cfg);
    }
    
    const LoRa_Config_t *cfg = LoRa_Service_Config_Get();
    
    // 2. 初始化驱动 (阻塞式，会进行 AT 握手)
    if (!LoRa_Driver_Init(cfg)) {
        LORA_LOG("[SVC] Driver Init Failed!\r\n");
        // 这里可以触发一个系统级错误事件
    }
    
    // 3. 初始化管理器
    LoRa_Manager_Init(cfg, _Service_OnRecv);
    
    // 4. 初始化监视器
    LoRa_Service_Monitor_Init();
    
    // 5. 通知应用层
    if (s_AppCb && s_AppCb->OnEvent) {
        s_AppCb->OnEvent(LORA_EVENT_INIT_SUCCESS, NULL);
    }
}

void LoRa_Service_Run(void) {
    // 1. 驱动层轮询 (如需)
    // LoRa_Driver_Run(); 
    
    // 2. 管理层轮询 (核心协议栈)
    LoRa_Manager_Run();
    
    // 3. 监视器轮询 (看门狗)
    LoRa_Service_Monitor_Run();
}

// [修改] 增加 opt 参数并透传
bool LoRa_Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id, LoRa_SendOpt_t opt) {
    LORA_CHECK(data && len > 0, false);
    // 透传给 Manager 层
    return LoRa_Manager_Send(data, len, target_id, opt);
}

uint32_t LoRa_Service_GetSleepDuration(void) {
    return LoRa_Manager_GetSleepDuration();
}

void LoRa_Service_FactoryReset(void) {
    LoRa_Service_Config_FactoryReset();
    if (s_AppCb && s_AppCb->OnEvent) {
        s_AppCb->OnEvent(LORA_EVENT_FACTORY_RESET, NULL);
    }
    // 建议重启
    if (s_AppCb && s_AppCb->SystemReset) {
        s_AppCb->SystemReset();
    }
}

const LoRa_Config_t* LoRa_Service_GetConfig(void) {
    return LoRa_Service_Config_Get();
}

void LoRa_Service_SetConfig(const LoRa_Config_t *cfg) {
    LoRa_Service_Config_Set(cfg);
    // 注意：修改配置后通常需要重启或重新初始化驱动才能生效
}

void LoRa_Service_NotifyEvent(LoRa_Event_t event, void *arg) {
    if (s_AppCb && s_AppCb->OnEvent) {
        s_AppCb->OnEvent(event, arg);
    }
}
