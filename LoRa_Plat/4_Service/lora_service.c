#include "lora_service.h"
#include "lora_manager.h"
#include "lora_driver.h" 
#include "lora_port.h" 
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

LoRa_Config_t g_LoRaConfig_Current; 
static LoRa_Config_t g_LoRaConfig_Pending; 
static LoRa_Callback_t g_cb; 

typedef enum { SVC_STATE_NORMAL = 0, SVC_STATE_CONFIGURING } SvcState_t;
static SvcState_t s_State = SVC_STATE_NORMAL;
static uint32_t   s_ConfigTimeoutTick = 0;
#define CONFIG_TIMEOUT_MS  30000 
#define CMD_PREFIX "CMD:"

static void _Svc_ProcessPlatformCmd(char *cmd_str);
static void _Svc_EnterConfigMode(uint32_t token);
static void _Svc_CommitConfig(void);
static void _Svc_AbortConfig(void);
static void _Svc_NotifyEvent(LoRa_Event_t event, void *arg);

// --- 接口实现 ---
const LoRa_Config_t* Service_GetConfig(void) {
    return &g_LoRaConfig_Current;
}

static void _On_Mgr_RxData(uint8_t *data, uint16_t len, uint16_t src_id, int16_t rssi) {
    if (len >= MGR_RX_BUF_SIZE) len = MGR_RX_BUF_SIZE - 1;
    data[len] = '\0';
    _Svc_NotifyEvent(LORA_EVENT_MSG_RECEIVED, data);
    
    if (len > strlen(CMD_PREFIX) && strncmp((char*)data, CMD_PREFIX, strlen(CMD_PREFIX)) == 0) {
        _Svc_ProcessPlatformCmd((char*)(data + strlen(CMD_PREFIX)));
    } else {
        if (g_cb.OnRecvData) {
            LoRa_RxMeta_t meta = { .rssi = -128, .snr = 0 };
            g_cb.OnRecvData(src_id, data, len, &meta);
        }
    }
}

static void _Svc_NotifyEvent(LoRa_Event_t event, void *arg) {
    if (g_cb.OnEvent) g_cb.OnEvent(event, arg);
}

void LoRa_Service_Init(const LoRa_Callback_t *callbacks, uint16_t override_net_id) {
    if (callbacks) g_cb = *callbacks;
    if (g_cb.LoadConfig) g_cb.LoadConfig(&g_LoRaConfig_Current);
    
    if (g_LoRaConfig_Current.magic != LORA_CFG_MAGIC) {
        if (g_cb.GetRandomSeed) srand(g_cb.GetRandomSeed());
        g_LoRaConfig_Current.uuid = ((uint32_t)rand() << 16) | rand();
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
    
    if (override_net_id != 0) g_LoRaConfig_Current.net_id = override_net_id;
    
    Manager_Init(g_LoRaConfig_Current.net_id, g_LoRaConfig_Current.group_id, 
                 _On_Mgr_RxData, NULL, NULL);
    
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
    bool need_ack = (target_id != 0xFFFF) && LORA_ENABLE_ACK;
    bool res = (Manager_Send(data, len, target_id, need_ack) == LORA_OK);
    if (res) _Svc_NotifyEvent(LORA_EVENT_MSG_SENT, NULL);
    return res;
}

void LoRa_Service_FactoryReset(void) {
    _Svc_NotifyEvent(LORA_EVENT_FACTORY_RESET, NULL);
    g_LoRaConfig_Current.magic = 0x00; 
    if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Current);
    Drv_SmartConfig();
    if (g_cb.SystemReset) g_cb.SystemReset();
}

static void _Svc_ProcessPlatformCmd(char *cmd_str) {
    char *cmd = strtok(cmd_str, "="); 
    char *params = strtok(NULL, "");  
    if (cmd == NULL) return;

    if (strcmp(cmd, "BIND") == 0 && params != NULL) {
        uint32_t target_uuid; uint16_t new_net_id;
        if (sscanf(params, "%u,%hu", &target_uuid, &new_net_id) == 2) {
            if (target_uuid == g_LoRaConfig_Current.uuid) {
                g_LoRaConfig_Current.net_id = new_net_id;
                if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Current);
                _Svc_NotifyEvent(LORA_EVENT_BIND_SUCCESS, &new_net_id);
                if (g_cb.SystemReset) g_cb.SystemReset();
            }
        }
    } else if (strcmp(cmd, "GROUP") == 0 && params != NULL) {
        uint16_t new_group_id;
        if (sscanf(params, "%hu", &new_group_id) == 1) {
            g_LoRaConfig_Current.group_id = new_group_id;
            if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Current);
            _Svc_NotifyEvent(LORA_EVENT_GROUP_UPDATE, &new_group_id);
            if (g_cb.SystemReset) g_cb.SystemReset();
        }
    } else if (strcmp(cmd, "CFG") == 0 && params != NULL) {
        int p_chan, p_pwr, p_rate;
        if (sscanf(params, "%d,%d,%d", &p_chan, &p_pwr, &p_rate) == 3) {
            g_LoRaConfig_Current.channel = (uint8_t)p_chan;
            g_LoRaConfig_Current.power = (uint8_t)p_pwr;
            g_LoRaConfig_Current.air_rate = (uint8_t)p_rate;
            if (g_cb.SaveConfig) g_cb.SaveConfig(&g_LoRaConfig_Current);
            _Svc_NotifyEvent(LORA_EVENT_CONFIG_COMMIT, NULL);
            if (g_cb.SystemReset) g_cb.SystemReset();
        }
    } else if (strcmp(cmd, "RST") == 0) {
        if (g_cb.SystemReset) g_cb.SystemReset();
    } else if (strcmp(cmd, "FACTORY") == 0) {
        LoRa_Service_FactoryReset();
    }
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
    s_State = SVC_STATE_NORMAL;
    _Svc_NotifyEvent(LORA_EVENT_CONFIG_COMMIT, NULL);
}

static void _Svc_AbortConfig(void) {
    s_State = SVC_STATE_NORMAL;
}
