#include "lora_app.h"
#include "lora_manager.h"
#include "lora_driver.h" 
#include "lora_port.h" 
#include "Flash.h"       
#include "cJSON.h"
#include "LED.h"         
#include <stdio.h>
#include <string.h>

// --- 全局配置对象 ---
LoRa_Config_t g_LoRaConfig_Current; 
LoRa_Config_t g_LoRaConfig_Pending; 

// --- 状态机 ---
typedef enum {
    APP_STATE_NORMAL = 0,
    APP_STATE_CONFIGURING
} AppState_t;

static AppState_t s_AppState = APP_STATE_NORMAL;
static uint32_t   s_ConfigTimeoutTick = 0;
#define CONFIG_TIMEOUT_MS  30000 

// --- 内部函数声明 ---
static void _App_HandleConfigCommand(cJSON *root);
static void _App_EnterConfigMode(uint32_t token);
static void _App_CommitConfig(void);
static void _App_AbortConfig(void);

// [新增] 工厂重置函数
void App_FactoryReset(void) {
    printf("[APP] !!! FACTORY RESET !!!\r\n");
    
    // 1. 恢复默认配置结构体
    g_LoRaConfig_Current.magic    = LORA_CFG_MAGIC;
    g_LoRaConfig_Current.token    = DEFAULT_LORA_TOKEN;
    g_LoRaConfig_Current.addr     = DEFAULT_LORA_ADDR;
    g_LoRaConfig_Current.channel  = DEFAULT_LORA_CHANNEL;
    g_LoRaConfig_Current.power    = (uint8_t)DEFAULT_LORA_POWER;
    g_LoRaConfig_Current.air_rate = (uint8_t)DEFAULT_LORA_RATE;
    g_LoRaConfig_Current.tmode    = (uint8_t)DEFAULT_LORA_TMODE;
    
    // 2. 写入 Flash
    Flash_WriteLoRaConfig(&g_LoRaConfig_Current);
    
    // 3. 调用驱动层救砖逻辑
    Drv_SmartConfig();
    
    // 4. 软件复位
    NVIC_SystemReset();
}

// --- 回调函数 ---
static void _App_OnRxData(uint8_t *data, uint16_t len, uint16_t src_id) {
    if (len >= MGR_RX_BUF_SIZE) len = MGR_RX_BUF_SIZE - 1;
    data[len] = '\0';
    
    printf("[APP] Rx from 0x%04X: %s\r\n", src_id, data);

    cJSON *root = cJSON_Parse((const char*)data);
    if (!root) {
        printf("[APP] JSON Parse Error\r\n");
        return;
    }

    cJSON *item_cmd = cJSON_GetObjectItem(root, "cmd");
    if (item_cmd && cJSON_IsString(item_cmd)) {
        char *cmd_str = item_cmd->valuestring;

        if (strncmp(cmd_str, "CFG_", 4) == 0) {
            _App_HandleConfigCommand(root);
        } 
        else if (strcmp(cmd_str, "LED_ON") == 0) {
            if (s_AppState == APP_STATE_NORMAL) {
                LED1_ON(); 
                printf("[APP] Action: LED ON\r\n");
            }
        }
        else if (strcmp(cmd_str, "LED_OFF") == 0) {
            if (s_AppState == APP_STATE_NORMAL) {
                LED1_OFF();
                printf("[APP] Action: LED OFF\r\n");
            }
        }
    }
    cJSON_Delete(root);
}

static void _App_HandleConfigCommand(cJSON *root) {
    cJSON *item_cmd = cJSON_GetObjectItem(root, "cmd");
    char *cmd = item_cmd->valuestring;

    if (strcmp(cmd, "CFG_START") == 0) {
        cJSON *item_token = cJSON_GetObjectItem(root, "token");
        if (item_token && cJSON_IsNumber(item_token)) {
            _App_EnterConfigMode((uint32_t)item_token->valuedouble);
        } else {
            printf("[APP] CFG Error: Token missing\r\n");
        }
    }
    else if (strcmp(cmd, "CFG_SET") == 0) {
        if (s_AppState != APP_STATE_CONFIGURING) return;
        s_ConfigTimeoutTick = GetTick(); 

        cJSON *item;
        item = cJSON_GetObjectItem(root, "addr");
        if (item) g_LoRaConfig_Pending.addr = (uint16_t)item->valuedouble;
        
        item = cJSON_GetObjectItem(root, "ch");
        if (item) g_LoRaConfig_Pending.channel = (uint8_t)item->valuedouble;
        
        item = cJSON_GetObjectItem(root, "pwr");
        if (item) g_LoRaConfig_Pending.power = (uint8_t)item->valuedouble;
        
        // [新增] 解析 tmode
        item = cJSON_GetObjectItem(root, "tmode");
        if (item) g_LoRaConfig_Pending.tmode = (uint8_t)item->valuedouble;
        
        printf("[APP] Pending Update: Ch=%d, Addr=0x%04X, Mode=%d\r\n", 
               g_LoRaConfig_Pending.channel, g_LoRaConfig_Pending.addr, g_LoRaConfig_Pending.tmode);
    }
    else if (strcmp(cmd, "CFG_COMMIT") == 0) {
        if (s_AppState == APP_STATE_CONFIGURING) {
            _App_CommitConfig();
        }
    }
    else if (strcmp(cmd, "CFG_ABORT") == 0) {
        _App_AbortConfig();
    }
    // [新增] 远程重置指令
    else if (strcmp(cmd, "CFG_RESET") == 0) {
        App_FactoryReset();
    }
}

static void _App_EnterConfigMode(uint32_t token) {
    if (token == g_LoRaConfig_Current.token) {
        s_AppState = APP_STATE_CONFIGURING;
        s_ConfigTimeoutTick = GetTick();
        memcpy(&g_LoRaConfig_Pending, &g_LoRaConfig_Current, sizeof(LoRa_Config_t));
        printf("[APP] Entered Config Mode.\r\n");
    } else {
        printf("[APP] CFG Denied: Invalid Token\r\n");
    }
}

static void _App_CommitConfig(void) {
    printf("[APP] Committing Config...\r\n");
    Flash_WriteLoRaConfig(&g_LoRaConfig_Pending);
    Drv_ApplyConfig(&g_LoRaConfig_Pending);
    memcpy(&g_LoRaConfig_Current, &g_LoRaConfig_Pending, sizeof(LoRa_Config_t));
    g_LoRaManager.local_id = g_LoRaConfig_Current.addr;
    s_AppState = APP_STATE_NORMAL;
    printf("[APP] Config Committed.\r\n");
}

static void _App_AbortConfig(void) {
    s_AppState = APP_STATE_NORMAL;
    printf("[APP] Config Aborted.\r\n");
}

void LoRa_App_Init(uint16_t override_local_id) {
    Flash_ReadLoRaConfig(&g_LoRaConfig_Current);
    
    if (override_local_id != 0) {
        g_LoRaConfig_Current.addr = override_local_id;
    }
    
    printf("[APP] Config Loaded: Addr=0x%04X, Ch=%d, Mode=%d\r\n", 
           g_LoRaConfig_Current.addr, g_LoRaConfig_Current.channel, g_LoRaConfig_Current.tmode);
    
    Manager_Init(_App_OnRxData, NULL, NULL);
    Drv_ApplyConfig(&g_LoRaConfig_Current);
    g_LoRaManager.local_id = g_LoRaConfig_Current.addr;
    Port_ClearRxBuffer();
}

void LoRa_App_Task(void) {
    Manager_Run();
    
    if (s_AppState == APP_STATE_CONFIGURING) {
        if (GetTick() - s_ConfigTimeoutTick > CONFIG_TIMEOUT_MS) {
            printf("[APP] Config Timeout. Auto-abort.\r\n");
            _App_AbortConfig();
        }
    }
}
