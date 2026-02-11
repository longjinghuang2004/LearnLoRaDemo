#include "lora_service.h"
#include "lora_manager.h"
#include "lora_driver.h" 
#include "lora_port.h" 
#include <string.h>
#include <stdlib.h> // for rand, srand, atoi
#include <stdio.h>  // for sscanf

// ============================================================
//                    内部变量
// ============================================================
LoRa_Config_t g_LoRaConfig_Current; 
static LoRa_Config_t g_LoRaConfig_Pending; 
static LoRa_Callback_t g_cb; 

// 状态机
typedef enum {
    SVC_STATE_NORMAL = 0,
    SVC_STATE_CONFIGURING
} SvcState_t;

static SvcState_t s_State = SVC_STATE_NORMAL;
static uint32_t   s_ConfigTimeoutTick = 0;
#define CONFIG_TIMEOUT_MS  30000 

// 指令前缀定义
#define CMD_PREFIX "CMD:"

// ============================================================
//                    内部函数声明
// ============================================================
static void _Svc_ProcessPlatformCmd(char *cmd_str);
static void _Svc_EnterConfigMode(uint32_t token);
static void _Svc_CommitConfig(void);
static void _Svc_AbortConfig(void);
static void _Svc_NotifyEvent(LoRa_Event_t event, void *arg);

// ============================================================
//                    回调适配层 (核心修改)
// ============================================================

// Manager 收到数据后的回调
static void _On_Mgr_RxData(uint8_t *data, uint16_t len, uint16_t src_id) {
    // 1. 确保字符串结束符 (安全)
    if (len >= MGR_RX_BUF_SIZE) len = MGR_RX_BUF_SIZE - 1;
    data[len] = '\0';
    
    // 2. [日志钩子] 无论是什么数据，先通知外部打印日志
    _Svc_NotifyEvent(LORA_EVENT_MSG_RECEIVED, data);
    
    // 3. [分流逻辑] 检查是否为平台指令 (CMD:...)
    // 使用 strncmp 进行高效前缀匹配
    if (len > strlen(CMD_PREFIX) && strncmp((char*)data, CMD_PREFIX, strlen(CMD_PREFIX)) == 0) {
        // ---> 进入平台指令处理通道
        // 跳过前缀 "CMD:"
        _Svc_ProcessPlatformCmd((char*)(data + strlen(CMD_PREFIX)));
    } 
    else {
        // ---> 进入用户业务处理通道
        if (g_cb.OnRecvData) {
            g_cb.OnRecvData(src_id, data, len);
        }
    }
}

static void _Svc_NotifyEvent(LoRa_Event_t event, void *arg) {
    if (g_cb.OnEvent) {
        g_cb.OnEvent(event, arg);
    }
}

// ============================================================
//                    核心逻辑实现
// ============================================================

void LoRa_Service_Init(const LoRa_Callback_t *callbacks, uint16_t override_net_id) {
    if (callbacks) {
        g_cb = *callbacks;
    }
    
    // 1. 读取配置
    if (g_cb.LoadConfig) {
        g_cb.LoadConfig(&g_LoRaConfig_Current);
    }
    
    // 2. 首次上电初始化 (生成 UUID)
    if (g_LoRaConfig_Current.magic != LORA_CFG_MAGIC) {
        if (g_cb.GetRandomSeed) srand(g_cb.GetRandomSeed());
        g_LoRaConfig_Current.uuid = ((uint32_t)rand() << 16) | rand();
        
        // 默认参数
        g_LoRaConfig_Current.magic    = LORA_CFG_MAGIC;
        g_LoRaConfig_Current.token    = DEFAULT_LORA_TOKEN;
        g_LoRaConfig_Current.net_id   = LORA_ID_UNASSIGNED;
        g_LoRaConfig_Current.hw_addr  = LORA_HW_ADDR_DEFAULT;
        g_LoRaConfig_Current.channel  = DEFAULT_LORA_CHANNEL;
        g_LoRaConfig_Current.power    = (uint8_t)DEFAULT_LORA_POWER;
        g_LoRaConfig_Current.air_rate = (uint8_t)DEFAULT_LORA_RATE;
        g_LoRaConfig_Current.tmode    = (uint8_t)DEFAULT_LORA_TMODE;
        
        if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Current);
    }
    
    // 3. 覆盖逻辑 (调试用)
    if (override_net_id != 0) {
        g_LoRaConfig_Current.net_id = override_net_id;
    }
    
    // 4. 初始化 Manager
    g_LoRaManager.local_id = g_LoRaConfig_Current.net_id;
    g_LoRaManager.uuid     = g_LoRaConfig_Current.uuid;
    Manager_Init(_On_Mgr_RxData, NULL, NULL); 
    
    // 5. 初始化 Driver
    Drv_ApplyConfig(&g_LoRaConfig_Current);
    Port_ClearRxBuffer();
    
    _Svc_NotifyEvent(LORA_EVENT_INIT_SUCCESS, NULL);
}

void LoRa_Service_Run(void) {
    Manager_Run();
    
    if (s_State == SVC_STATE_CONFIGURING) {
        if (g_cb.GetTick && (g_cb.GetTick() - s_ConfigTimeoutTick > CONFIG_TIMEOUT_MS)) {
            _Svc_AbortConfig();
        }
    }
}

bool LoRa_Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id) {
    bool res = Manager_SendPacket(data, len, target_id);
    if (res) {
        _Svc_NotifyEvent(LORA_EVENT_MSG_SENT, NULL);
    }
    return res;
}

void LoRa_Service_FactoryReset(void) {
    _Svc_NotifyEvent(LORA_EVENT_FACTORY_RESET, NULL);
    g_LoRaConfig_Current.magic = 0x00; 
    if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Current);
    Drv_SmartConfig();
    if (g_cb.SystemReset) g_cb.SystemReset();
}

// ============================================================
//                    平台指令解析 (去 cJSON 化)
// ============================================================

// 输入字符串格式示例: "BIND=12345678,2" 或 "RST"
static void _Svc_ProcessPlatformCmd(char *cmd_str) {
    char *cmd = strtok(cmd_str, "="); // 分割指令名和参数
    char *params = strtok(NULL, "");  // 获取剩余部分作为参数

    if (cmd == NULL) return;

    // 1. BIND 指令: CMD:BIND=<uuid>,<new_id>
    if (strcmp(cmd, "BIND") == 0 && params != NULL) {
        uint32_t target_uuid;
        uint16_t new_net_id;
        // 使用 sscanf 解析参数
        if (sscanf(params, "%u,%hu", &target_uuid, &new_net_id) == 2) {
            if (target_uuid == g_LoRaConfig_Current.uuid) {
                g_LoRaConfig_Current.net_id = new_net_id;
                if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Current);
                g_LoRaManager.local_id = new_net_id;
                _Svc_NotifyEvent(LORA_EVENT_BIND_SUCCESS, &new_net_id);
                if (g_cb.SystemReset) g_cb.SystemReset();
            }
        }
    }
    // 2. RST 指令: CMD:RST
    else if (strcmp(cmd, "RST") == 0) {
        _Svc_NotifyEvent(LORA_EVENT_REBOOT_REQ, NULL);
        if (g_cb.SystemReset) g_cb.SystemReset();
    }
    // 3. FACTORY 指令: CMD:FACTORY
    else if (strcmp(cmd, "FACTORY") == 0) {
        LoRa_Service_FactoryReset();
    }
    // 4. CFG 指令: CMD:CFG=<net_id>,<chan>,<pwr> (进入配置模式并暂存)
    else if (strcmp(cmd, "CFG") == 0 && params != NULL) {
        // 只有在配置模式下才允许设置? 或者直接设置? 
        // 这里简化逻辑：收到 CFG 直接进入配置态并暂存
        if (s_State != SVC_STATE_CONFIGURING) {
             _Svc_EnterConfigMode(g_LoRaConfig_Current.token); // 简化：忽略Token校验或默认Token
        }
        
        uint16_t p_netid;
        uint16_t p_chan;
        uint16_t p_pwr;
        
        // 使用 %hu 读取 uint16 (short)
        if (sscanf(params, "%hu,%hu,%hu", &p_netid, &p_chan, &p_pwr) == 3) {
            g_LoRaConfig_Pending.net_id = p_netid;
            g_LoRaConfig_Pending.channel = (uint8_t)p_chan;
            g_LoRaConfig_Pending.power = (uint8_t)p_pwr;
            // 自动提交
            _Svc_CommitConfig();
        }
    }
    // 5. INFO 指令: CMD:INFO (暂未实现回传，可在此扩展)
}

static void _Svc_EnterConfigMode(uint32_t token) {
    s_State = SVC_STATE_CONFIGURING;
    if (g_cb.GetTick) s_ConfigTimeoutTick = g_cb.GetTick();
    memcpy(&g_LoRaConfig_Pending, &g_LoRaConfig_Current, sizeof(LoRa_Config_t));
    _Svc_NotifyEvent(LORA_EVENT_CONFIG_START, NULL);
}

static void _Svc_CommitConfig(void) {
    if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Pending);
    Drv_ApplyConfig(&g_LoRaConfig_Pending);
    memcpy(&g_LoRaConfig_Current, &g_LoRaConfig_Pending, sizeof(LoRa_Config_t));
    g_LoRaManager.local_id = g_LoRaConfig_Current.net_id;
    s_State = SVC_STATE_NORMAL;
    _Svc_NotifyEvent(LORA_EVENT_CONFIG_COMMIT, NULL);
}

static void _Svc_AbortConfig(void) {
    s_State = SVC_STATE_NORMAL;
}
