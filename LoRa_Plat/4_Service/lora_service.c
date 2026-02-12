#include "lora_service.h"
#include "lora_manager.h"
#include "lora_driver.h" 
#include "lora_port.h" 
#include "Serial.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

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
//                    回调适配层
// ============================================================

// Manager 收到数据后的回调
static void _On_Mgr_RxData(uint8_t *data, uint16_t len, uint16_t src_id) {
    // 1. 安全处理
    if (len >= MGR_RX_BUF_SIZE) len = MGR_RX_BUF_SIZE - 1;
    data[len] = '\0';
    
    // 2. 日志通知
    _Svc_NotifyEvent(LORA_EVENT_MSG_RECEIVED, data);
    
    // 3. 分流逻辑
    if (len > strlen(CMD_PREFIX) && strncmp((char*)data, CMD_PREFIX, strlen(CMD_PREFIX)) == 0) {
        // ---> 平台指令通道
        _Svc_ProcessPlatformCmd((char*)(data + strlen(CMD_PREFIX)));
    } 
    else {
        // ---> 业务数据通道
        if (g_cb.OnRecvData) {
            // 构造默认元数据 (目前硬件不支持读取RSSI，填默认值)
            LoRa_RxMeta_t meta;
            meta.rssi = -128; // 无效值
            meta.snr = 0;
            
            g_cb.OnRecvData(src_id, data, len, &meta);
        }
    }
}

// Manager 发送结果回调
static void _On_Mgr_TxResult(bool success) {
    if (success) {
        _Svc_NotifyEvent(LORA_EVENT_MSG_SENT, NULL);
    }
}

// Manager 错误回调
static void _On_Mgr_Error(LoRaError_t err) {
    // 这里可以将错误码转换为事件通知 App
    // 目前简化处理，仅在调试日志中体现
    Serial_Printf("[SVC] Mgr Error: %d\r\n", err);
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
    
    // 2. 首次上电初始化 (默认参数)
    if (g_LoRaConfig_Current.magic != LORA_CFG_MAGIC) {
        Serial_Printf("[SVC] Config Invalid, Resetting to Default...\r\n");
        if (g_cb.GetRandomSeed) srand(g_cb.GetRandomSeed());
        g_LoRaConfig_Current.uuid = ((uint32_t)rand() << 16) | rand();
        
        // 默认参数
        g_LoRaConfig_Current.magic    = LORA_CFG_MAGIC;
        g_LoRaConfig_Current.token    = DEFAULT_LORA_TOKEN;
        g_LoRaConfig_Current.net_id   = LORA_ID_UNASSIGNED;
        g_LoRaConfig_Current.group_id = LORA_GROUP_ID_DEFAULT; 
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
    
    // 4. 初始化 Manager (协议层)
    g_LoRaManager.local_id = g_LoRaConfig_Current.net_id;
    g_LoRaManager.group_id = g_LoRaConfig_Current.group_id; 
    g_LoRaManager.uuid     = g_LoRaConfig_Current.uuid;
    Manager_Init(_On_Mgr_RxData, _On_Mgr_TxResult, _On_Mgr_Error); 
    
    // 5. 初始化 Driver (驱动层 - 阻塞式，含救砖)
    Serial_Printf("[SVC] Init Driver (Blocking)...\r\n");
    
    // 这里会阻塞 1-3 秒，直到硬件握手成功
    if (Drv_Init(&g_LoRaConfig_Current)) {
        Serial_Printf("[SVC] Driver Init OK\r\n");
        Port_ClearRxBuffer(); // 清空初始化过程中的残留数据
        _Svc_NotifyEvent(LORA_EVENT_INIT_SUCCESS, NULL);
    } else {
        Serial_Printf("[SVC] Driver Init FAIL! System Halted.\r\n");
        // 初始化失败，死循环闪灯报警 (SOS模式)
        // 实际项目中可能需要看门狗复位
        while(1) {
            // 这里假设外部有 LED 控制，或者直接卡死
            for(volatile int i=0; i<1000000; i++);
        }
    }
}

void LoRa_Service_Run(void) {
    // 1. 协议栈心跳
    Manager_Run();
    
    // 2. 配置模式超时检查
    if (s_State == SVC_STATE_CONFIGURING) {
        if (g_cb.GetTick && (g_cb.GetTick() - s_ConfigTimeoutTick > CONFIG_TIMEOUT_MS)) {
            Serial_Printf("[SVC] Config Timeout, Abort.\r\n");
            _Svc_AbortConfig();
        }
    }
}

bool LoRa_Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id) {
    // 调用 Manager 发送
    // Manager 内部会检查 AUX 忙闲，如果忙则返回 false
    bool res = Manager_SendPacket(data, len, target_id);
    return res;
}

void LoRa_Service_FactoryReset(void) {
    Serial_Printf("[SVC] Factory Reset Triggered.\r\n");
    _Svc_NotifyEvent(LORA_EVENT_FACTORY_RESET, NULL);
    
    g_LoRaConfig_Current.magic = 0x00; // 破坏 Magic
    if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Current);
    
    // 复位系统
    if (g_cb.SystemReset) g_cb.SystemReset();
}

// ============================================================
//                    平台指令解析
// ============================================================

static void _Svc_ProcessPlatformCmd(char *cmd_str) {
    char *cmd = strtok(cmd_str, "="); 
    char *params = strtok(NULL, "");  

    if (cmd == NULL) return;

    Serial_Printf("[SVC] Platform Cmd: %s\r\n", cmd);

    // 1. BIND 指令: CMD:BIND=<uuid>,<new_id>
    if (strcmp(cmd, "BIND") == 0 && params != NULL) {
        uint32_t target_uuid;
        uint16_t new_net_id;
        if (sscanf(params, "%u,%hu", &target_uuid, &new_net_id) == 2) {
            if (target_uuid == g_LoRaConfig_Current.uuid) {
                g_LoRaConfig_Current.net_id = new_net_id;
                if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Current);
                
                g_LoRaManager.local_id = new_net_id; // 实时更新 Manager
                
                _Svc_NotifyEvent(LORA_EVENT_BIND_SUCCESS, &new_net_id);
                
                // 绑定后通常建议重启生效
                if (g_cb.SystemReset) g_cb.SystemReset();
            }
        }
    }
    // 2. GROUP 指令: CMD:GROUP=<new_group_id>
    else if (strcmp(cmd, "GROUP") == 0 && params != NULL) {
        uint16_t new_group_id;
        if (sscanf(params, "%hu", &new_group_id) == 1) {
            g_LoRaConfig_Current.group_id = new_group_id;
            if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Current);
            
            g_LoRaManager.group_id = new_group_id; // 实时更新 Manager
            
            _Svc_NotifyEvent(LORA_EVENT_GROUP_UPDATE, &new_group_id);
        }
    }
    // 3. RST 指令
    else if (strcmp(cmd, "RST") == 0) {
        _Svc_NotifyEvent(LORA_EVENT_REBOOT_REQ, NULL);
        if (g_cb.SystemReset) g_cb.SystemReset();
    }
    // 4. FACTORY 指令
    else if (strcmp(cmd, "FACTORY") == 0) {
        LoRa_Service_FactoryReset();
    }
    // 5. CFG 指令 (远程配置参数)
    else if (strcmp(cmd, "CFG") == 0 && params != NULL) {
        // 格式: CMD:CFG=<netid>,<chan>,<pwr>
        if (s_State != SVC_STATE_CONFIGURING) {
             _Svc_EnterConfigMode(g_LoRaConfig_Current.token);
        }
        uint16_t p_netid, p_chan, p_pwr;
        if (sscanf(params, "%hu,%hu,%hu", &p_netid, &p_chan, &p_pwr) == 3) {
            g_LoRaConfig_Pending.net_id = p_netid;
            g_LoRaConfig_Pending.channel = (uint8_t)p_chan;
            g_LoRaConfig_Pending.power = (uint8_t)p_pwr;
            _Svc_CommitConfig();
        }
    }
}

static void _Svc_EnterConfigMode(uint32_t token) {
    s_State = SVC_STATE_CONFIGURING;
    if (g_cb.GetTick) s_ConfigTimeoutTick = g_cb.GetTick();
    // 备份当前配置到 Pending
    memcpy(&g_LoRaConfig_Pending, &g_LoRaConfig_Current, sizeof(LoRa_Config_t));
    _Svc_NotifyEvent(LORA_EVENT_CONFIG_START, NULL);
}

static void _Svc_CommitConfig(void) {
    // 保存新配置
    if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Pending);
    
    // 这里的 Drv_Init 是阻塞的，会重新握手并应用参数
    // 注意：运行时调用阻塞函数会导致业务中断 1-2 秒，但在配置模式下是可以接受的
    Drv_Init(&g_LoRaConfig_Pending);
    
    // 更新当前配置
    memcpy(&g_LoRaConfig_Current, &g_LoRaConfig_Pending, sizeof(LoRa_Config_t));
    g_LoRaManager.local_id = g_LoRaConfig_Current.net_id;
    g_LoRaManager.group_id = g_LoRaConfig_Current.group_id; 
    
    s_State = SVC_STATE_NORMAL;
    _Svc_NotifyEvent(LORA_EVENT_CONFIG_COMMIT, NULL);
}

static void _Svc_AbortConfig(void) {
    s_State = SVC_STATE_NORMAL;
}


// [新增] 实现获取配置接口
const LoRa_Config_t* LoRa_Service_GetConfig(void) {
    return &g_LoRaConfig_Current;
}
