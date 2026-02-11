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
// 1 = 主机 (HOST): ID=1, 发送指令
// 2 = 从机 (NODE): ID=2, 响应指令
// ============================================================================
#define TEST_ROLE      2  

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
void Adapter_OnRecvData(uint16_t src_id, const uint8_t *data, uint16_t len) {
    // 此时 data 已经是纯净的业务数据，不含 CMD: 前缀
    
    // 场景1：文本指令解析
    if (strstr((const char*)data, "LED_ON")) {
        LED1_ON(); 
        printf("    [Action] LED ON\r\n");
    }
    else if (strstr((const char*)data, "LED_OFF")) {
        LED1_OFF();
        printf("    [Action] LED OFF\r\n");
    }
    
    // 场景2：二进制解析示例 (假设协议: 0xA5 <Cmd> <Val>)
    // if (len >= 3 && data[0] == 0xA5) {
    //     uint8_t cmd = data[1];
    //     uint8_t val = data[2];
    //     printf("    [Binary] Cmd:0x%02X Val:0x%02X\r\n", cmd, val);
    // }
}

// LoRa 事件回调 (用于日志)
void Adapter_OnEvent(LoRa_Event_t event, void *arg) {
    switch(event) {
        case LORA_EVENT_INIT_SUCCESS:
            printf("[EVT] Init OK. UUID:0x%08X, NetID:%d\r\n", 
                   g_LoRaConfig_Current.uuid, g_LoRaConfig_Current.net_id);
            break;
        case LORA_EVENT_MSG_RECEIVED:
            // 这里打印所有收到的原始数据 (包括 CMD: 指令)
            printf("[EVT] RX Raw: %s\r\n", (char*)arg);
            LED1_Turn(); // 闪灯指示接收
            break;
        case LORA_EVENT_BIND_SUCCESS:
            printf("[EVT] BIND OK! New NetID: %d\r\n", *(uint16_t*)arg);
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
// ============================================================

// [强制初始化] 确保测试环境 ID 正确
void Force_Init_Config(void) {
    LoRa_Config_t cfg;
    Flash_ReadLoRaConfig(&cfg);
    
    uint16_t target_id = (TEST_ROLE == 1) ? 1 : 2;
    
    // 如果当前 Flash 里的 ID 不是目标 ID，或者 Magic 不对，强制重写
    if (cfg.net_id != target_id || cfg.magic != LORA_CFG_MAGIC) {
        printf("[TEST] Forcing NetID to %d...\r\n", target_id);
        
        // 构造默认配置
        memset(&cfg, 0, sizeof(LoRa_Config_t));
        cfg.magic = LORA_CFG_MAGIC;
        cfg.net_id = target_id;
        cfg.uuid = (TEST_ROLE == 1) ? 0xAAAA1111 : 0xBBBB2222; // 固定UUID方便测试
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
    printf("\r\n=== LoRaPlat V2.1 (No-JSON) ===\r\n");
    printf("Role: %s (ID=%d)\r\n", (TEST_ROLE==1)?"HOST":"SLAVE", (TEST_ROLE==1)?1:2);
    printf("1. CMD0 <msg>       : Send to ID 0\r\n");
    printf("2. CMD <id> <msg>   : Send to ID (e.g., CMD 2 LED_ON)\r\n");
    printf("3. BIND <uuid> <id> : Send CMD:BIND=<uuid>,<id>\r\n");
    printf("4. RST <id>         : Send CMD:RST to ID\r\n");
    printf("===============================\r\n");
}

// ============================================================================
// 3. 主函数
// ============================================================================

int main(void)
{
    SysTick_Init();
    LED_Init();
    Serial_Init();
    
    // 1. 强制初始化配置 (测试用)
    Force_Init_Config();

    // 2. 初始化服务 (传入0，使用Flash中的配置)
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
            // 去除换行
            int len = strlen(input);
            while(len > 0 && (input[len-1] == '\r' || input[len-1] == '\n')) input[--len] = '\0';

            if (len > 0) {
                printf("PC Input: %s\r\n", input);

                // --- 发送业务指令 ---
                if (strncmp(input, "CMD ", 4) == 0) {
                    int target_id;
                    char msg[64];
                    if (sscanf(input + 4, "%d %[^\n]", &target_id, msg) == 2) {
                        printf(" -> Send to %d: %s\r\n", target_id, msg);
                        LoRa_Service_Send((uint8_t*)msg, strlen(msg), target_id);
                    }
                }
                // --- 发送平台指令: BIND ---
                else if (strncmp(input, "BIND ", 5) == 0) {
                    uint32_t u;
                    int id;
                    if (sscanf(input + 5, "%u %d", &u, &id) == 2) {
                        char cmd[64];
                        sprintf(cmd, "CMD:BIND=%u,%d", u, id);
                        printf(" -> Send Platform Cmd: %s\r\n", cmd);
                        LoRa_Service_Send((uint8_t*)cmd, strlen(cmd), 0); // 发给ID 0
                    }
                }
                // --- 发送平台指令: RST ---
                else if (strncmp(input, "RST ", 4) == 0) {
                    int target_id;
                    if (sscanf(input + 4, "%d", &target_id) == 1) {
                        char *cmd = "CMD:RST";
                        printf(" -> Send RST to %d\r\n", target_id);
                        LoRa_Service_Send((uint8_t*)cmd, strlen(cmd), target_id);
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
