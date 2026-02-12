/**
  ******************************************************************************
  * @file    lora_service.c
  * @author  LoRaPlat Team
  * @brief   Layer 4: 业务服务层实现
  ******************************************************************************
  */

#include "lora_service.h"
#include "lora_manager.h"
#include "lora_driver.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h> // sscanf

// ============================================================
//                    1. 内部变量
// ============================================================

static LoRa_App_Adapter_t s_Adapter;
static LoRa_Config_t      s_Config;

#define CMD_PREFIX "CMD:"

// ============================================================
//                    2. 内部辅助函数
// ============================================================

static void _NotifyEvent(LoRa_Event_t evt, void *arg) {
    if (s_Adapter.OnEvent) {
        s_Adapter.OnEvent(evt, arg);
    }
}

// 处理平台指令 (如 CMD:BIND=...)
static void _ProcessPlatformCmd(char *cmd_str) {
    char *cmd = strtok(cmd_str, "=");
    char *params = strtok(NULL, "");

    if (cmd == NULL) return;

    // 1. BIND 指令: CMD:BIND=<uuid>,<new_id>
    if (strcmp(cmd, "BIND") == 0 && params != NULL) {
        uint32_t target_uuid;
        uint16_t new_net_id;
        if (sscanf(params, "%u,%hu", &target_uuid, &new_net_id) == 2) {
            if (target_uuid == s_Config.uuid) {
                s_Config.net_id = new_net_id;
                if (s_Adapter.SaveConfig) s_Adapter.SaveConfig(&s_Config);
                _NotifyEvent(LORA_EVT_BIND_SUCCESS, &new_net_id);
                if (s_Adapter.SystemReset) s_Adapter.SystemReset();
            }
        }
    }
    // 2. RST 指令: CMD:RST
    else if (strcmp(cmd, "RST") == 0) {
        if (s_Adapter.SystemReset) s_Adapter.SystemReset();
    }
    // 3. FACTORY 指令: CMD:FACTORY
    else if (strcmp(cmd, "FACTORY") == 0) {
        Service_FactoryReset();
    }
}

// Manager 接收回调
static void _On_Mgr_Recv(uint8_t *data, uint16_t len, uint16_t src_id, int16_t rssi) {
    // 1. 安全处理字符串
    if (len >= MGR_RX_BUF_SIZE) len = MGR_RX_BUF_SIZE - 1;
    data[len] = '\0';

    // 2. 检查是否是平台指令
    if (strncmp((char*)data, CMD_PREFIX, strlen(CMD_PREFIX)) == 0) {
        _ProcessPlatformCmd((char*)(data + strlen(CMD_PREFIX)));
    } 
    else {
        // 3. 透传给 App
        if (s_Adapter.OnRecvData) {
            LoRa_RxMeta_t meta = { .rssi = rssi, .snr = 0 };
            s_Adapter.OnRecvData(src_id, data, len, &meta);
        }
        _NotifyEvent(LORA_EVT_MSG_RECV, NULL);
    }
}

// Manager 发送结果回调
static void _On_Mgr_TxResult(bool success) {
    if (success) {
        _NotifyEvent(LORA_EVT_MSG_SENT, NULL);
    }
}

// Manager 错误回调
static void _On_Mgr_Error(LoRa_Result_t err) {
    // 这里可以将错误码转换为事件通知 App
    // 目前简化处理，仅在调试日志中体现
}

// ============================================================
//                    3. 核心接口实现
// ============================================================

void Service_Init(const LoRa_App_Adapter_t *adapter, uint16_t override_net_id) {
    if (adapter) s_Adapter = *adapter;
    
    // 1. 加载配置
    if (s_Adapter.LoadConfig) {
        s_Adapter.LoadConfig(&s_Config);
    }
    
    // 2. 校验 Magic，无效则生成默认配置
    if (s_Config.magic != LORA_CFG_MAGIC) {
        if (s_Adapter.GetRandomSeed) srand(s_Adapter.GetRandomSeed());
        s_Config.uuid = ((uint32_t)rand() << 16) | rand();
        
        s_Config.magic    = LORA_CFG_MAGIC;
        s_Config.net_id   = LORA_ID_UNASSIGNED;
        s_Config.group_id = LORA_GROUP_ID_DEFAULT;
        s_Config.hw_addr  = LORA_HW_ADDR_DEFAULT;
        s_Config.channel  = DEFAULT_LORA_CHANNEL;
        s_Config.power    = DEFAULT_LORA_POWER;
        s_Config.air_rate = DEFAULT_LORA_RATE;
        s_Config.tmode    = DEFAULT_LORA_TMODE;
        
        if (s_Adapter.SaveConfig) s_Adapter.SaveConfig(&s_Config);
    }
    
    // 3. 覆盖 NetID (调试用)
    if (override_net_id != 0) {
        s_Config.net_id = override_net_id;
    }
    
    // 4. 初始化协议栈
    Manager_Init(s_Config.net_id, s_Config.group_id, 
                 _On_Mgr_Recv, _On_Mgr_TxResult, _On_Mgr_Error);
    
    // 5. 应用配置到驱动 (异步)
    // 注意：这里我们假设 Driver 默认参数与 Config 一致，或者在 Init 后立即 Config
    // 实际项目中，应该调用 Drv_AsyncConfig() 并传入参数
    // 目前 V3.0 简化为使用 driver_config.c 中的静态表
    Drv_AsyncConfig(); 
    
    _NotifyEvent(LORA_EVT_INIT_DONE, NULL);
}

void Service_Run(void) {
    Manager_Run();
}

bool Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id) {
    // 默认开启 ACK (除非是广播)
    bool need_ack = (target_id != 0xFFFF) && LORA_ENABLE_ACK;
    
    LoRa_Result_t res = Manager_Send(data, len, target_id, need_ack);
    return (res == LORA_OK);
}

bool Service_IsIdle(void) {
    return Manager_IsIdle();
}

void Service_FactoryReset(void) {
    s_Config.magic = 0x00; // 破坏 Magic
    if (s_Adapter.SaveConfig) s_Adapter.SaveConfig(&s_Config);
    _NotifyEvent(LORA_EVT_FACTORY_RESET, NULL);
    if (s_Adapter.SystemReset) s_Adapter.SystemReset();
}

const LoRa_Config_t* Service_GetConfig(void) {
    return &s_Config;
}

