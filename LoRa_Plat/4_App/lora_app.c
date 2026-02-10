
#include "lora_app.h"
#include "lora_manager.h"
#include "lora_driver.h" 
#include "lora_port.h" 
#include "Flash.h"       
#include "cJSON.h"
#include "LED.h"         
#include <stdio.h>
#include <string.h>
#include <stdlib.h> // for rand, srand

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
static void _App_PrintIdentity(void);

// [新增] 打印当前身份信息
static void _App_PrintIdentity(void) {
    printf("[APP] Identity Info:\r\n");
    printf("      UUID:   0x%08X\r\n", g_LoRaConfig_Current.uuid);
    printf("      NetID:  0x%04X %s\r\n", g_LoRaConfig_Current.net_id, 
           (g_LoRaConfig_Current.net_id == LORA_ID_UNASSIGNED) ? "(Unassigned)" : "");
    printf("      HWAddr: 0x%04X\r\n", g_LoRaConfig_Current.hw_addr);
}

// [修改] 工厂重置
void App_FactoryReset(void) {
    printf("[APP] !!! FACTORY RESET !!!\r\n");
    
    // 1. 擦除 Magic，下次重启会重新生成 UUID
    g_LoRaConfig_Current.magic = 0x00; 
    Flash_WriteLoRaConfig(&g_LoRaConfig_Current);
    
    // 2. 调用驱动层救砖逻辑 (恢复模块出厂设置)
    Drv_SmartConfig();
    
    // 3. 软件复位
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

    // 1. 绑定指令 (CFG_BIND) - 仅当未分配或强制重绑时有效
    //    Payload: {"cmd":"CFG_BIND", "uuid":12345678, "new_id":1}
    if (strcmp(cmd, "CFG_BIND") == 0) {
        cJSON *item_uuid = cJSON_GetObjectItem(root, "uuid");
        cJSON *item_newid = cJSON_GetObjectItem(root, "new_id");
        
        if (item_uuid && item_newid) {
            uint32_t target_uuid = (uint32_t)item_uuid->valuedouble;
            uint16_t new_net_id = (uint16_t)item_newid->valuedouble;
            
            // 检查 UUID 是否匹配
            if (target_uuid == g_LoRaConfig_Current.uuid) {
                printf("[APP] BIND MATCH! New NetID: 0x%04X\r\n", new_net_id);
                
                // 更新配置
                g_LoRaConfig_Current.net_id = new_net_id;
                Flash_WriteLoRaConfig(&g_LoRaConfig_Current);
                
                // 立即生效
                g_LoRaManager.local_id = new_net_id;
                
                // 重启以确保万无一失
                printf("[APP] Rebooting to apply ID...\r\n");
                Delay_ms(100);
                NVIC_SystemReset();
            }
        }
    }
    // 2. 进入配置模式
    else if (strcmp(cmd, "CFG_START") == 0) {
        cJSON *item_token = cJSON_GetObjectItem(root, "token");
        if (item_token && cJSON_IsNumber(item_token)) {
            _App_EnterConfigMode((uint32_t)item_token->valuedouble);
        } else {
            printf("[APP] CFG Error: Token missing\r\n");
        }
    }
    // 3. 设置参数 (CFG_SET)
    else if (strcmp(cmd, "CFG_SET") == 0) {
        if (s_AppState != APP_STATE_CONFIGURING) return;
        s_ConfigTimeoutTick = GetTick(); 

        cJSON *item;
        // [修改] 支持设置 NetID 和 HWAddr
        item = cJSON_GetObjectItem(root, "net_id");
        if (item) g_LoRaConfig_Pending.net_id = (uint16_t)item->valuedouble;
        
        item = cJSON_GetObjectItem(root, "hw_addr");
        if (item) g_LoRaConfig_Pending.hw_addr = (uint16_t)item->valuedouble;
        
        item = cJSON_GetObjectItem(root, "ch");
        if (item) g_LoRaConfig_Pending.channel = (uint8_t)item->valuedouble;
        
        item = cJSON_GetObjectItem(root, "pwr");
        if (item) g_LoRaConfig_Pending.power = (uint8_t)item->valuedouble;
        
        item = cJSON_GetObjectItem(root, "tmode");
        if (item) g_LoRaConfig_Pending.tmode = (uint8_t)item->valuedouble;
        
        printf("[APP] Pending Update: NetID=0x%04X, HWAddr=0x%04X, Ch=%d\r\n", 
               g_LoRaConfig_Pending.net_id, g_LoRaConfig_Pending.hw_addr, g_LoRaConfig_Pending.channel);
    }
    else if (strcmp(cmd, "CFG_COMMIT") == 0) {
        if (s_AppState == APP_STATE_CONFIGURING) {
            _App_CommitConfig();
        }
    }
    else if (strcmp(cmd, "CFG_ABORT") == 0) {
        _App_AbortConfig();
    }
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
    
    // 更新 Manager 的身份
    g_LoRaManager.local_id = g_LoRaConfig_Current.net_id;
    
    s_AppState = APP_STATE_NORMAL;
    printf("[APP] Config Committed.\r\n");
}

static void _App_AbortConfig(void) {
    s_AppState = APP_STATE_NORMAL;
    printf("[APP] Config Aborted.\r\n");
}

// [修改] 初始化函数
void LoRa_App_Init(uint16_t override_local_id) {
    Flash_ReadLoRaConfig(&g_LoRaConfig_Current);
    
    // 1. 首次上电初始化 (生成 UUID)
    if (g_LoRaConfig_Current.magic != LORA_CFG_MAGIC) {
        printf("[APP] First Boot detected! Initializing...\r\n");
        
        // 生成随机 UUID
        srand(Port_GetRandomSeed());
        // 组合两次 rand() 以确保覆盖 32 位
        g_LoRaConfig_Current.uuid = ((uint32_t)rand() << 16) | rand();
        
        // 设置默认值
        g_LoRaConfig_Current.magic    = LORA_CFG_MAGIC;
        g_LoRaConfig_Current.token    = DEFAULT_LORA_TOKEN;
        g_LoRaConfig_Current.net_id   = LORA_ID_UNASSIGNED; // 默认未分配
        g_LoRaConfig_Current.hw_addr  = LORA_HW_ADDR_DEFAULT; // 默认物理全通
        g_LoRaConfig_Current.channel  = DEFAULT_LORA_CHANNEL;
        g_LoRaConfig_Current.power    = (uint8_t)DEFAULT_LORA_POWER;
        g_LoRaConfig_Current.air_rate = (uint8_t)DEFAULT_LORA_RATE;
        g_LoRaConfig_Current.tmode    = (uint8_t)DEFAULT_LORA_TMODE;
        
        // 写入 Flash
        Flash_WriteLoRaConfig(&g_LoRaConfig_Current);
        printf("[APP] Generated UUID: 0x%08X\r\n", g_LoRaConfig_Current.uuid);
    }
    
    // 2. 覆盖逻辑 (调试用)
    if (override_local_id != 0) {
        g_LoRaConfig_Current.net_id = override_local_id;
    }
    
    _App_PrintIdentity();
    
    // 3. 配置 Manager
    g_LoRaManager.local_id = g_LoRaConfig_Current.net_id;
    g_LoRaManager.uuid     = g_LoRaConfig_Current.uuid;
    Manager_Init(_App_OnRxData, NULL, NULL);
    
    // 4. 配置 Driver (应用物理参数)
    Drv_ApplyConfig(&g_LoRaConfig_Current);
    
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
