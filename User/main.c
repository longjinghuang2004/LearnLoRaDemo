#include "stm32f10x.h"
#include "Delay.h"
#include "Serial.h"
#include "LED.h"
#include "Flash.h"
#include "lora_service.h" 
#include <string.h>
#include <stdio.h>

volatile uint8_t g_TimeoutFlag;

// ============================================================================
// [测试角色配置]
// 1 = 主机 (HOST): ID=1, Group=100, 发送指令
// 2 = 从机 (NODE): ID=2, Group=100, 响应指令
// ============================================================================
#define TEST_ROLE      1  

// ============================================================================
// 1. 接口适配
// ============================================================================

void Adapter_SaveConfig(const LoRa_Config_t *cfg) {
    Flash_WriteLoRaConfig(cfg);
}

void Adapter_LoadConfig(LoRa_Config_t *cfg) {
    Flash_ReadLoRaConfig(cfg);
}

uint32_t Adapter_GetTick(void) {
    return GetTick();
}

uint32_t Adapter_GetRandomSeed(void) {
    extern uint32_t Port_GetRandomSeed(void);
    return Port_GetRandomSeed();
}

void Adapter_SystemReset(void) {
    printf("[SYS] System Resetting...\r\n");
    Delay_ms(100);
    NVIC_SystemReset();
}

// [业务钩子] 收到非平台指令的数据时调用
// [V2.2 更新] 增加 meta 参数
void Adapter_OnRecvData(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta) {
    // 打印元数据 (目前硬件不支持读取RSSI，显示默认值)
    // printf("[APP] From:0x%04X RSSI:%d SNR:%d | Payload: %s\r\n", src_id, meta->rssi, meta->snr, data);
    
    // 业务逻辑处理
    if (strstr((const char*)data, "LED_ON")) {
        LED1_ON(); 
        printf("    [Action] LED ON (From ID: %d)\r\n", src_id);
    }
    else if (strstr((const char*)data, "LED_OFF")) {
        LED1_OFF();
        printf("    [Action] LED OFF (From ID: %d)\r\n", src_id);
    }
    else if (strstr((const char*)data, "HELLO")) {
        LED1_Turn();
        printf("    [Action] HELLO received\r\n");
    }
}

// LoRa 事件回调
void Adapter_OnEvent(LoRa_Event_t event, void *arg) {
    switch(event) {
        case LORA_EVENT_INIT_SUCCESS:
            printf("[EVT] Init OK. UUID:0x%08X, NetID:%d, GroupID:%d\r\n", 
                   g_LoRaConfig_Current.uuid, g_LoRaConfig_Current.net_id, g_LoRaConfig_Current.group_id);
            break;
        case LORA_EVENT_MSG_RECEIVED:
            printf("[EVT] RX Raw: %s\r\n", (char*)arg);
            LED1_Turn(); 
            break;
        case LORA_EVENT_BIND_SUCCESS:
            printf("[EVT] BIND OK! New NetID: %d\r\n", *(uint16_t*)arg);
            break;
        case LORA_EVENT_GROUP_UPDATE:
            printf("[EVT] GROUP Updated! New GroupID: %d\r\n", *(uint16_t*)arg);
            break;
        case LORA_EVENT_CONFIG_COMMIT:
            printf("[EVT] Config Updated.\r\n");
            break;
        default: break;
    }
}

const LoRa_Callback_t my_callbacks = {
    .SaveConfig     = Adapter_SaveConfig,
    .LoadConfig     = Adapter_LoadConfig,
    .GetTick        = Adapter_GetTick,
    .GetRandomSeed  = Adapter_GetRandomSeed,
    .SystemReset    = Adapter_SystemReset,
    .OnRecvData     = Adapter_OnRecvData,
    .OnEvent        = Adapter_OnEvent
};

// ============================================================================
// 2. 辅助函数
// ============================================================================

// [强制初始化] 确保测试环境 ID 和 Group 正确
void Force_Init_Config(void) {
    LoRa_Config_t cfg;
    Flash_ReadLoRaConfig(&cfg);
    
    uint16_t target_id = (TEST_ROLE == 1) ? 1 : 2;
    uint16_t target_group = 100; // 默认都在 100 组
    
    // 检查 ID, Group, Magic 是否匹配
    if (cfg.net_id != target_id || cfg.group_id != target_group || cfg.magic != LORA_CFG_MAGIC) {
        printf("[TEST] Forcing Config: NetID=%d, GroupID=%d...\r\n", target_id, target_group);
        
        memset(&cfg, 0, sizeof(LoRa_Config_t));
        cfg.magic = LORA_CFG_MAGIC;
        cfg.net_id = target_id;
        cfg.group_id = target_group; 
        cfg.uuid = (TEST_ROLE == 1) ? 0xAAAA1111 : 0xBBBB2222;
        cfg.hw_addr = LORA_HW_ADDR_DEFAULT;
        cfg.channel = DEFAULT_LORA_CHANNEL;
        cfg.power = DEFAULT_LORA_POWER;
        cfg.air_rate = DEFAULT_LORA_RATE;
        cfg.tmode = DEFAULT_LORA_TMODE;
        
        Flash_WriteLoRaConfig(&cfg);
        printf("[TEST] Force Init Done. Rebooting...\r\n");
        Delay_ms(100);
        NVIC_SystemReset();
    }
}

void Show_Help(void) {
    printf("\r\n=== LoRaPlat V2.2 (Group Support) ===\r\n");
    printf("Role: %s (ID=%d, Group=%d)\r\n", (TEST_ROLE==1)?"HOST":"SLAVE", g_LoRaConfig_Current.net_id, g_LoRaConfig_Current.group_id);
    printf("1. CMD <id> <msg>       : Unicast (e.g., CMD 2 LED_ON)\r\n");
    printf("2. CMD <gid> <msg>      : Multicast to Group (e.g., CMD 100 LED_ON)\r\n");
    printf("3. SETGRP <id> <new_gid>: Remote Set Group (e.g., SETGRP 2 200)\r\n");
    printf("4. BIND <uuid> <id>     : Remote Bind ID\r\n");
    printf("=====================================\r\n");
}

// ============================================================================
// 3. 主函数
// ============================================================================

int main(void)
{
    SysTick_Init();
    LED_Init();
    Serial_Init();
    
    // 1. 强制初始化配置
    Force_Init_Config();

    // 2. 初始化服务
    LoRa_Service_Init(&my_callbacks, 0); 

#if (TEST_ROLE == 1)
    Show_Help();
#endif

    while (1)
    {
        LoRa_Service_Run();

#if (TEST_ROLE == 1) // 主机逻辑
        if (Serial_RxFlag == 1)
        {
            char *input = Serial_RxPacket;
            int len = strlen(input);
            while(len > 0 && (input[len-1] == '\r' || input[len-1] == '\n')) input[--len] = '\0';

            if (len > 0) {
                printf("PC Input: %s\r\n", input);

                // --- 1. 通用发送 (单播/组播) ---
                // 格式: CMD <target_id> <msg>
                // 如果 target_id 是 2，则是单播给从机
                // 如果 target_id 是 100，则是组播给 Group 100
                if (strncmp(input, "CMD ", 4) == 0) {
                    int target_id;
                    char msg[64];
                    if (sscanf(input + 4, "%d %[^\n]", &target_id, msg) == 2) {
                        printf(" -> Send to %d: %s\r\n", target_id, msg);
                        LoRa_Service_Send((uint8_t*)msg, strlen(msg), target_id);
                    }
                }
                // --- 2. 远程设置组ID (平台指令) ---
                // 格式: SETGRP <target_id> <new_group_id>
                // 发送: CMD:GROUP=<new_group_id>
                else if (strncmp(input, "SETGRP ", 7) == 0) {
                    int target_id;
                    int new_gid;
                    if (sscanf(input + 7, "%d %d", &target_id, &new_gid) == 2) {
                        char cmd[64];
                        sprintf(cmd, "CMD:GROUP=%d", new_gid);
                        printf(" -> Send Platform Cmd to %d: %s\r\n", target_id, cmd);
                        LoRa_Service_Send((uint8_t*)cmd, strlen(cmd), target_id);
                    }
                }
                // --- 3. 绑定指令 ---
                else if (strncmp(input, "BIND ", 5) == 0) {
                    uint32_t u;
                    int id;
                    if (sscanf(input + 5, "%u %d", &u, &id) == 2) {
                        char cmd[64];
                        sprintf(cmd, "CMD:BIND=%u,%d", u, id);
                        printf(" -> Send Bind Cmd: %s\r\n", cmd);
                        LoRa_Service_Send((uint8_t*)cmd, strlen(cmd), 0); 
                    }
                }
                else if (strcasecmp(input, "HELP") == 0) {
                    Show_Help();
                }
            }
            Serial_RxFlag = 0; 
        }
#endif
    }
}
