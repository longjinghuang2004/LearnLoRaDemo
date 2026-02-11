#include "lora_service.h"
#include "lora_manager.h"
#include "lora_driver.h" 
#include "lora_port.h" // 仅用于 Port_ClearRxBuffer，未来也可抽象
#include "cJSON.h"
#include <string.h>
#include <stdlib.h> // for rand, srand

// ============================================================
//                    内部变量
// ============================================================
LoRa_Config_t g_LoRaConfig_Current; 
static LoRa_Config_t g_LoRaConfig_Pending; 
static LoRa_Callback_t g_cb; // 保存外部传入的回调

// 状态机
typedef enum {
    SVC_STATE_NORMAL = 0,
    SVC_STATE_CONFIGURING
} SvcState_t;

static SvcState_t s_State = SVC_STATE_NORMAL;
static uint32_t   s_ConfigTimeoutTick = 0;
#define CONFIG_TIMEOUT_MS  30000 

// ============================================================
//                    内部函数声明
// ============================================================
static void _Svc_HandleConfigCommand(cJSON *root);
static void _Svc_EnterConfigMode(uint32_t token);
static void _Svc_CommitConfig(void);
static void _Svc_AbortConfig(void);
static void _Svc_NotifyEvent(LoRa_Event_t event, void *arg);

// ============================================================
//                    回调适配层
// ============================================================

// Manager 收到数据后的回调
static void _On_Mgr_RxData(uint8_t *data, uint16_t len, uint16_t src_id) {
    // 1. 确保字符串结束符 (安全)
    if (len >= MGR_RX_BUF_SIZE) len = MGR_RX_BUF_SIZE - 1;
    data[len] = '\0';
    
    // 2. 通知外部 (用于日志或业务处理)
    _Svc_NotifyEvent(LORA_EVENT_MSG_RECEIVED, data);
    
    // 3. 尝试解析为 JSON 指令
    cJSON *root = cJSON_Parse((const char*)data);
    if (root) {
        cJSON *item_cmd = cJSON_GetObjectItem(root, "cmd");
        if (item_cmd && cJSON_IsString(item_cmd)) {
            char *cmd_str = item_cmd->valuestring;
            
            // 如果是配置指令 (CFG_...)，由 Service 层拦截处理
            if (strncmp(cmd_str, "CFG_", 4) == 0) {
                _Svc_HandleConfigCommand(root);
            } 
            // 其他指令，透传给用户层
            else {
                if (g_cb.OnRecvData) {
                    g_cb.OnRecvData(src_id, data, len);
                }
            }
        } else {
            // 不是 JSON 命令，直接透传
            if (g_cb.OnRecvData) {
                g_cb.OnRecvData(src_id, data, len);
            }
        }
        cJSON_Delete(root);
    } else {
        // 解析失败 (非JSON数据)，直接透传
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
    
    // 1. 读取配置 (通过回调)
    if (g_cb.LoadConfig) {
        g_cb.LoadConfig(&g_LoRaConfig_Current);
    }
    
    // 2. 首次上电初始化 (生成 UUID)
    if (g_LoRaConfig_Current.magic != LORA_CFG_MAGIC) {
        // 初始化随机数种子
        if (g_cb.GetRandomSeed) {
            srand(g_cb.GetRandomSeed());
        }
        
        // 生成 UUID
        g_LoRaConfig_Current.uuid = ((uint32_t)rand() << 16) | rand();
        
        // 设置默认值
        g_LoRaConfig_Current.magic    = LORA_CFG_MAGIC;
        g_LoRaConfig_Current.token    = DEFAULT_LORA_TOKEN;
        g_LoRaConfig_Current.net_id   = LORA_ID_UNASSIGNED;
        g_LoRaConfig_Current.hw_addr  = LORA_HW_ADDR_DEFAULT;
        g_LoRaConfig_Current.channel  = DEFAULT_LORA_CHANNEL;
        g_LoRaConfig_Current.power    = (uint8_t)DEFAULT_LORA_POWER;
        g_LoRaConfig_Current.air_rate = (uint8_t)DEFAULT_LORA_RATE;
        g_LoRaConfig_Current.tmode    = (uint8_t)DEFAULT_LORA_TMODE;
        
        // 保存配置
        if (g_cb.SaveConfig) {
            g_cb.SaveConfig(&g_LoRaConfig_Current);
        }
    }
    
    // 3. 覆盖逻辑 (调试用)
    if (override_net_id != 0) {
        g_LoRaConfig_Current.net_id = override_net_id;
    }
    
    // 4. 初始化 Manager
    g_LoRaManager.local_id = g_LoRaConfig_Current.net_id;
    g_LoRaManager.uuid     = g_LoRaConfig_Current.uuid;
    Manager_Init(_On_Mgr_RxData, NULL, NULL); // 注册内部回调
    
    // 5. 初始化 Driver (应用物理参数)
    Drv_ApplyConfig(&g_LoRaConfig_Current);
    
    Port_ClearRxBuffer();
    
    _Svc_NotifyEvent(LORA_EVENT_INIT_SUCCESS, NULL);
}

void LoRa_Service_Run(void) {
    Manager_Run();
    
    // 配置模式超时处理
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
    
    // 1. 擦除 Magic
    g_LoRaConfig_Current.magic = 0x00; 
    if (g_cb.SaveConfig) {
        g_cb.SaveConfig(&g_LoRaConfig_Current);
    }
    
    // 2. 恢复模块出厂设置
    Drv_SmartConfig();
    
    // 3. 重启
    if (g_cb.SystemReset) {
        g_cb.SystemReset();
    }
}

// ============================================================
//                    配置指令处理
// ============================================================

static void _Svc_HandleConfigCommand(cJSON *root) {
    cJSON *item_cmd = cJSON_GetObjectItem(root, "cmd");
    char *cmd = item_cmd->valuestring;

    // 1. 绑定指令 (CFG_BIND)
    if (strcmp(cmd, "CFG_BIND") == 0) {
        cJSON *item_uuid = cJSON_GetObjectItem(root, "uuid");
        cJSON *item_newid = cJSON_GetObjectItem(root, "new_id");
        
        if (item_uuid && item_newid) {
            uint32_t target_uuid = (uint32_t)item_uuid->valuedouble;
            uint16_t new_net_id = (uint16_t)item_newid->valuedouble;
            
            if (target_uuid == g_LoRaConfig_Current.uuid) {
                // 更新配置
                g_LoRaConfig_Current.net_id = new_net_id;
                if (g_cb.SaveConfig) {
                    g_cb.SaveConfig(&g_LoRaConfig_Current);
                }
                
                // 立即生效
                g_LoRaManager.local_id = new_net_id;
                
                _Svc_NotifyEvent(LORA_EVENT_BIND_SUCCESS, &new_net_id);
                
                // 请求重启
                if (g_cb.SystemReset) {
                    // 简单的延时可以通过空循环实现，或者依赖外部实现延时
                    // 这里直接调用重启，外部实现应负责必要的延时
                    g_cb.SystemReset();
                }
            }
        }
    }
    // 2. 进入配置模式
    else if (strcmp(cmd, "CFG_START") == 0) {
        cJSON *item_token = cJSON_GetObjectItem(root, "token");
        if (item_token && cJSON_IsNumber(item_token)) {
            _Svc_EnterConfigMode((uint32_t)item_token->valuedouble);
        }
    }
    // 3. 设置参数
    else if (strcmp(cmd, "CFG_SET") == 0) {
        if (s_State != SVC_STATE_CONFIGURING) return;
        if (g_cb.GetTick) s_ConfigTimeoutTick = g_cb.GetTick();

        cJSON *item;
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
    }
    // 4. 提交配置
    else if (strcmp(cmd, "CFG_COMMIT") == 0) {
        if (s_State == SVC_STATE_CONFIGURING) {
            _Svc_CommitConfig();
        }
    }
    // 5. 取消配置
    else if (strcmp(cmd, "CFG_ABORT") == 0) {
        _Svc_AbortConfig();
    }
    // 6. 远程重置
    else if (strcmp(cmd, "CFG_RESET") == 0) {
        LoRa_Service_FactoryReset();
    }
}

static void _Svc_EnterConfigMode(uint32_t token) {
    if (token == g_LoRaConfig_Current.token) {
        s_State = SVC_STATE_CONFIGURING;
        if (g_cb.GetTick) s_ConfigTimeoutTick = g_cb.GetTick();
        memcpy(&g_LoRaConfig_Pending, &g_LoRaConfig_Current, sizeof(LoRa_Config_t));
        _Svc_NotifyEvent(LORA_EVENT_CONFIG_START, NULL);
    }
}

static void _Svc_CommitConfig(void) {
    if (g_cb.SaveConfig) {
        g_cb.SaveConfig(&g_LoRaConfig_Pending);
    }
    Drv_ApplyConfig(&g_LoRaConfig_Pending);
    memcpy(&g_LoRaConfig_Current, &g_LoRaConfig_Pending, sizeof(LoRa_Config_t));
    
    g_LoRaManager.local_id = g_LoRaConfig_Current.net_id;
    
    s_State = SVC_STATE_NORMAL;
    _Svc_NotifyEvent(LORA_EVENT_CONFIG_COMMIT, NULL);
}

static void _Svc_AbortConfig(void) {
    s_State = SVC_STATE_NORMAL;
}
