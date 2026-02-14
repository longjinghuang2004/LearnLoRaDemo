#include "lora_service.h"
#include "lora_manager.h"
#include "lora_driver.h" 
#include "lora_port.h" 
#include "lora_osal.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>

// ============================================================
//                    内部变量
// ============================================================
LoRa_Config_t g_LoRaConfig_Current; 
static LoRa_Config_t g_LoRaConfig_Pending; 
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
    
    // 1. 加载默认参数 (Hardcoded Defaults)
    memset(&g_LoRaConfig_Current, 0, sizeof(LoRa_Config_t));
    
    g_LoRaConfig_Current.magic    = LORA_CFG_MAGIC;
    g_LoRaConfig_Current.token    = DEFAULT_LORA_TOKEN;
    g_LoRaConfig_Current.net_id   = LORA_ID_UNASSIGNED;
    g_LoRaConfig_Current.group_id = LORA_GROUP_ID_DEFAULT; 
    g_LoRaConfig_Current.hw_addr  = LORA_HW_ADDR_DEFAULT;
    g_LoRaConfig_Current.channel  = DEFAULT_LORA_CHANNEL;
    g_LoRaConfig_Current.power    = (uint8_t)DEFAULT_LORA_POWER;
    g_LoRaConfig_Current.air_rate = (uint8_t)DEFAULT_LORA_RATE;
    g_LoRaConfig_Current.tmode    = (uint8_t)DEFAULT_LORA_TMODE;
    
    if (g_cb.GetRandomSeed) {
        srand(g_cb.GetRandomSeed());
        g_LoRaConfig_Current.uuid = ((uint32_t)rand() << 16) | rand();
    }

    // 2. 尝试从 Flash 加载覆盖
#if (defined(LORA_ENABLE_FLASH_SAVE) && LORA_ENABLE_FLASH_SAVE == 1)
    if (g_cb.LoadConfig) {
        LoRa_Config_t flash_cfg;
        g_cb.LoadConfig(&flash_cfg);
        
        if (flash_cfg.magic == LORA_CFG_MAGIC) {
            LORA_LOG("[SVC] Config Loaded from Flash.\r\n");
            memcpy(&g_LoRaConfig_Current, &flash_cfg, sizeof(LoRa_Config_t));
        } else {
            LORA_LOG("[SVC] Flash Empty/Invalid. Writing Defaults...\r\n");
            if (g_cb.SaveConfig) {
                g_cb.SaveConfig(&g_LoRaConfig_Current);
            }
        }
    }
#else
    LORA_LOG("[SVC] Flash Save Disabled. Using Default Config.\r\n");
#endif

    // 3. 调试覆盖
    if (override_net_id != 0) {
        g_LoRaConfig_Current.net_id = override_net_id;
        LORA_LOG("[SVC] NetID Overridden to: %d\r\n", override_net_id);
    }
    
    // 4. 初始化各层
    g_LoRaManager.local_id = g_LoRaConfig_Current.net_id;
    g_LoRaManager.group_id = g_LoRaConfig_Current.group_id; 
    g_LoRaManager.uuid     = g_LoRaConfig_Current.uuid;
    
    Manager_Init(_On_Mgr_RxData, _On_Mgr_TxResult, _On_Mgr_Error); 
    
    LORA_LOG("[SVC] Init Driver (Blocking)...\r\n");
    
    if (Drv_Init(&g_LoRaConfig_Current)) {
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
    return Manager_SendPacket(data, len, target_id);
}

void LoRa_Service_FactoryReset(void) {
    LORA_LOG("[SVC] Factory Reset Triggered.\r\n");
    _Svc_NotifyEvent(LORA_EVENT_FACTORY_RESET, NULL);
    
#if (defined(LORA_ENABLE_FLASH_SAVE) && LORA_ENABLE_FLASH_SAVE == 1)
    g_LoRaConfig_Current.magic = 0x00; 
    if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Current);
#endif
    
    if (g_cb.SystemReset) g_cb.SystemReset();
}

const LoRa_Config_t* LoRa_Service_GetConfig(void) {
    return &g_LoRaConfig_Current;
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
            if (target_uuid == g_LoRaConfig_Current.uuid) {
                g_LoRaConfig_Current.net_id = new_net_id;
                g_LoRaManager.local_id = new_net_id; 
                
            #if (defined(LORA_ENABLE_FLASH_SAVE) && LORA_ENABLE_FLASH_SAVE == 1)
                if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Current);
            #endif
                
                _Svc_NotifyEvent(LORA_EVENT_BIND_SUCCESS, &new_net_id);
                if (g_cb.SystemReset) g_cb.SystemReset();
            }
        }
    }
    else if (strcmp(cmd, "GROUP") == 0 && params != NULL) {
        uint16_t new_group_id;
        if (sscanf(params, "%hu", &new_group_id) == 1) {
            g_LoRaConfig_Current.group_id = new_group_id;
            g_LoRaManager.group_id = new_group_id;
            
            #if (defined(LORA_ENABLE_FLASH_SAVE) && LORA_ENABLE_FLASH_SAVE == 1)
                if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Current);
            #endif
            
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
    s_ConfigTimeoutTick = OSAL_GetTick();
    memcpy(&g_LoRaConfig_Pending, &g_LoRaConfig_Current, sizeof(LoRa_Config_t));
    _Svc_NotifyEvent(LORA_EVENT_CONFIG_START, NULL);
}

static void _Svc_CommitConfig(void) {
    #if (defined(LORA_ENABLE_FLASH_SAVE) && LORA_ENABLE_FLASH_SAVE == 1)
        if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Pending);
    #endif
    
    Drv_Init(&g_LoRaConfig_Pending);
    
    memcpy(&g_LoRaConfig_Current, &g_LoRaConfig_Pending, sizeof(LoRa_Config_t));
    g_LoRaManager.local_id = g_LoRaConfig_Current.net_id;
    g_LoRaManager.group_id = g_LoRaConfig_Current.group_id; 
    
    s_State = SVC_STATE_NORMAL;
    _Svc_NotifyEvent(LORA_EVENT_CONFIG_COMMIT, NULL);
}

static void _Svc_AbortConfig(void) {
    s_State = SVC_STATE_NORMAL;
}
