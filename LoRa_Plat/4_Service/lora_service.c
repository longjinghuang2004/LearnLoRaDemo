#include "lora_service.h"
#include "lora_manager.h"
#include "lora_driver.h" 
#include "lora_port.h" 
#include "lora_osal.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ============================================================
//                    内部变量 (私有化)
// ============================================================
static LoRa_Config_t s_LoRaConfig; // [修改] static
static LoRa_Config_t s_LoRaConfig_Pending; 
static LoRa_Callback_t g_cb; 

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

static void _On_Mgr_RxData(uint8_t *data, uint16_t len, uint16_t src_id) {
    if (len >= MGR_RX_BUF_SIZE) len = MGR_RX_BUF_SIZE - 1;
    data[len] = '\0';
    
    _Svc_NotifyEvent(LORA_EVENT_MSG_RECEIVED, data);
    
    if (len > strlen(CMD_PREFIX) && strncmp((char*)data, CMD_PREFIX, strlen(CMD_PREFIX)) == 0) {
        _Svc_ProcessPlatformCmd((char*)(data + strlen(CMD_PREFIX)));
    } 
    else {
        if (g_cb.OnRecvData) {
            LoRa_RxMeta_t meta = { .rssi = -128, .snr = 0 };
            g_cb.OnRecvData(src_id, data, len, &meta);
        }
    }
}

static void _On_Mgr_TxResult(bool success) {
    if (success) {
        _Svc_NotifyEvent(LORA_EVENT_MSG_SENT, NULL);
    }
}

static void _On_Mgr_Error(LoRaError_t err) {
    LORA_LOG("[SVC] Mgr Error: %d\r\n", err);
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
    
    // 1. 加载默认参数
    memset(&s_LoRaConfig, 0, sizeof(LoRa_Config_t));
    
    s_LoRaConfig.magic    = LORA_CFG_MAGIC;
    s_LoRaConfig.token    = DEFAULT_LORA_TOKEN;
    s_LoRaConfig.net_id   = LORA_ID_UNASSIGNED;
    s_LoRaConfig.group_id = LORA_GROUP_ID_DEFAULT; 
    s_LoRaConfig.hw_addr  = LORA_HW_ADDR_DEFAULT;
    s_LoRaConfig.channel  = DEFAULT_LORA_CHANNEL;
    s_LoRaConfig.power    = (uint8_t)DEFAULT_LORA_POWER;
    s_LoRaConfig.air_rate = (uint8_t)DEFAULT_LORA_RATE;
    s_LoRaConfig.tmode    = (uint8_t)DEFAULT_LORA_TMODE;
    
    if (g_cb.GetRandomSeed) {
        srand(g_cb.GetRandomSeed());
        s_LoRaConfig.uuid = ((uint32_t)rand() << 16) | rand();
    }

    // 2. 尝试从 Flash 加载覆盖
#if (defined(LORA_ENABLE_FLASH_SAVE) && LORA_ENABLE_FLASH_SAVE == 1)
    if (g_cb.LoadConfig) {
        LoRa_Config_t flash_cfg;
        g_cb.LoadConfig(&flash_cfg);
        
        if (flash_cfg.magic == LORA_CFG_MAGIC) {
            LORA_LOG("[SVC] Config Loaded from Flash.\r\n");
            memcpy(&s_LoRaConfig, &flash_cfg, sizeof(LoRa_Config_t));
        } else {
            LORA_LOG("[SVC] Flash Empty/Invalid. Writing Defaults...\r\n");
            if (g_cb.SaveConfig) {
                g_cb.SaveConfig(&s_LoRaConfig);
            }
        }
    }
#else
    LORA_LOG("[SVC] Flash Save Disabled. Using Default Config.\r\n");
#endif

    // 3. 调试覆盖
    if (override_net_id != 0) {
        s_LoRaConfig.net_id = override_net_id;
        LORA_LOG("[SVC] NetID Overridden to: %d\r\n", override_net_id);
    }
    
    // 4. 初始化各层
    // 注意：Manager_Init 不再直接访问 Config，而是通过参数传递 ID
    // 但 Manager 内部可能需要 ID，这里我们通过 SetConfig 传递给 Manager (如果 Manager 有接口的话)
    // 或者 Manager 依然保留 local_id 成员，由 Service 初始化时赋值
    
    // [修改] Manager 变量私有化后，我们需要通过接口设置 ID
    Manager_SetIdentity(s_LoRaConfig.net_id, s_LoRaConfig.group_id, s_LoRaConfig.uuid);
    
    Manager_Init(_On_Mgr_RxData, _On_Mgr_TxResult, _On_Mgr_Error); 
    
    LORA_LOG("[SVC] Init Driver (Blocking)...\r\n");
    
    if (Drv_Init(&s_LoRaConfig)) {
        LORA_LOG("[SVC] Driver Init OK\r\n");
        Port_ClearRxBuffer(); 
        _Svc_NotifyEvent(LORA_EVENT_INIT_SUCCESS, NULL);
    } else {
        LORA_LOG("[SVC] Driver Init FAIL! System Halted.\r\n");
        while(1); 
    }
}

void LoRa_Service_Run(void) {
    Manager_Run();
    
    if (s_State == SVC_STATE_CONFIGURING) {
        if ((OSAL_GetTick() - s_ConfigTimeoutTick > CONFIG_TIMEOUT_MS)) {
            LORA_LOG("[SVC] Config Timeout, Abort.\r\n");
            _Svc_AbortConfig();
        }
    }
}

bool LoRa_Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id) {
    // [修改] 传递当前配置中的信道和模式给 Manager (如果需要)
    // 这里假设 Manager 内部已经有了必要的配置，或者通过 SetIdentity 设置了
    return Manager_SendPacket(data, len, target_id);
}

void LoRa_Service_FactoryReset(void) {
    LORA_LOG("[SVC] Factory Reset Triggered.\r\n");
    _Svc_NotifyEvent(LORA_EVENT_FACTORY_RESET, NULL);
    
#if (defined(LORA_ENABLE_FLASH_SAVE) && LORA_ENABLE_FLASH_SAVE == 1)
    s_LoRaConfig.magic = 0x00; 
    if (g_cb.SaveConfig) g_cb.SaveConfig(&s_LoRaConfig);
#endif
    
    if (g_cb.SystemReset) g_cb.SystemReset();
}

const LoRa_Config_t* LoRa_Service_GetConfig(void) {
    return &s_LoRaConfig;
}

void LoRa_Service_SetConfig(const LoRa_Config_t *cfg) {
    if (cfg == NULL) return;
    
    OSAL_EnterCritical();
    memcpy(&s_LoRaConfig, cfg, sizeof(LoRa_Config_t));
    OSAL_ExitCritical();
    
    // 同步更新 Manager 的身份信息
    Manager_SetIdentity(s_LoRaConfig.net_id, s_LoRaConfig.group_id, s_LoRaConfig.uuid);
    
    // 如果开启了 Flash，自动保存
#if (defined(LORA_ENABLE_FLASH_SAVE) && LORA_ENABLE_FLASH_SAVE == 1)
    if (g_cb.SaveConfig) {
        g_cb.SaveConfig(&s_LoRaConfig);
    }
#endif
}

// ============================================================
//                    平台指令解析
// ============================================================

static void _Svc_ProcessPlatformCmd(char *cmd_str) {
    char *cmd = strtok(cmd_str, "="); 
    char *params = strtok(NULL, "");  

    if (cmd == NULL) return;

    LORA_LOG("[SVC] Platform Cmd: %s\r\n", cmd);

    if (strcmp(cmd, "BIND") == 0 && params != NULL) {
        uint32_t target_uuid;
        uint16_t new_net_id;
        if (sscanf(params, "%u,%hu", &target_uuid, &new_net_id) == 2) {
            if (target_uuid == s_LoRaConfig.uuid) {
                s_LoRaConfig.net_id = new_net_id;
                
                // [修改] 使用 SetConfig 统一更新并保存
                LoRa_Service_SetConfig(&s_LoRaConfig);
                
                _Svc_NotifyEvent(LORA_EVENT_BIND_SUCCESS, &new_net_id);
                if (g_cb.SystemReset) g_cb.SystemReset();
            }
        }
    }
    else if (strcmp(cmd, "GROUP") == 0 && params != NULL) {
        uint16_t new_group_id;
        if (sscanf(params, "%hu", &new_group_id) == 1) {
            s_LoRaConfig.group_id = new_group_id;
            
            // [修改] 使用 SetConfig 统一更新并保存
            LoRa_Service_SetConfig(&s_LoRaConfig);
            
            _Svc_NotifyEvent(LORA_EVENT_GROUP_UPDATE, &new_group_id);
        }
    }
    else if (strcmp(cmd, "RST") == 0) {
        _Svc_NotifyEvent(LORA_EVENT_REBOOT_REQ, NULL);
        if (g_cb.SystemReset) g_cb.SystemReset();
    }
    else if (strcmp(cmd, "FACTORY") == 0) {
        LoRa_Service_FactoryReset();
    }
    else if (strcmp(cmd, "CFG") == 0 && params != NULL) {
        if (s_State != SVC_STATE_CONFIGURING) {
             _Svc_EnterConfigMode(s_LoRaConfig.token);
        }
        uint16_t p_netid, p_chan, p_pwr;
        if (sscanf(params, "%hu,%hu,%hu", &p_netid, &p_chan, &p_pwr) == 3) {
            s_LoRaConfig_Pending.net_id = p_netid;
            s_LoRaConfig_Pending.channel = (uint8_t)p_chan;
            s_LoRaConfig_Pending.power = (uint8_t)p_pwr;
            _Svc_CommitConfig();
        }
    }
}

static void _Svc_EnterConfigMode(uint32_t token) {
    s_State = SVC_STATE_CONFIGURING;
    s_ConfigTimeoutTick = OSAL_GetTick();
    memcpy(&s_LoRaConfig_Pending, &s_LoRaConfig, sizeof(LoRa_Config_t));
    _Svc_NotifyEvent(LORA_EVENT_CONFIG_START, NULL);
}

static void _Svc_CommitConfig(void) {
    // [修改] 使用 SetConfig 统一更新并保存
    // 注意：Drv_Init 需要 Pending 配置，所以先 Init 再 SetConfig
    
    Drv_Init(&s_LoRaConfig_Pending);
    
    LoRa_Service_SetConfig(&s_LoRaConfig_Pending);
    
    s_State = SVC_STATE_NORMAL;
    _Svc_NotifyEvent(LORA_EVENT_CONFIG_COMMIT, NULL);
}

static void _Svc_AbortConfig(void) {
    s_State = SVC_STATE_NORMAL;
}
