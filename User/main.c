/**
  ******************************************************************************
  * @file    main.c
  * @author  LoRaPlat Team
  * @brief   LoRaPlat V3.0 最终修复版
  ******************************************************************************
  */

#include "stm32f10x.h"
#include "Delay.h"
#include "Serial.h" 
#include "LED.h"
#include "Flash.h"
#include "lora_service.h" 
#include "lora_port.h"
#include <string.h>
#include <stdio.h>

// [修复] 补回 Timer.c 依赖的全局变量
volatile uint8_t g_TimeoutFlag = 0;

// ============================================================================
// [测试角色配置]
// 1 = HOST (主机)
// 2 = SLAVE (从机)
// ============================================================================
#define TEST_ROLE      2

// ============================================================================
// 1. 接口适配 (Adapter Layer)
// ============================================================================

void Adapter_SaveConfig(const LoRa_Config_t *cfg) {
    Flash_WriteLoRaConfig(cfg);
    Serial_Printf("[APP] Config Saved.\r\n");
}

void Adapter_LoadConfig(LoRa_Config_t *cfg) {
    Flash_ReadLoRaConfig(cfg);
}

uint32_t Adapter_GetTick(void) {
    return GetTick();
}

uint32_t Adapter_GetRandomSeed(void) {
    return GetTick() ^ (*(uint32_t*)0x20000000); 
}

void Adapter_SystemReset(void) {
    Serial_Printf("[APP] Resetting...\r\n");
    for(volatile int i=0; i<1000000; i++); 
    NVIC_SystemReset();
}

// [关键] 接收数据回调
void Adapter_OnRecvData(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta) {
    Serial_Printf("[APP] RX from ID:0x%04X | Payload: %s\r\n", src_id, data);
    
    if (strstr((const char*)data, "LED_ON")) {
        LED1_ON(); 
        Serial_Printf("    -> Action: LED ON\r\n");
    } else if (strstr((const char*)data, "LED_OFF")) {
        LED1_OFF();
        Serial_Printf("    -> Action: LED OFF\r\n");
    }

    // [从机模式] 自动回响
#if (TEST_ROLE == 2)
    if (src_id != 0xFFFF) {
        char reply[64];
        snprintf(reply, 64, "Echo: %s", data);
        
        // 使用 LoRa_Service_Send
        if (!LoRa_Service_Send((uint8_t*)reply, strlen(reply), src_id)) {
            Serial_Printf("[APP] Echo Failed: Busy\r\n");
        } else {
            Serial_Printf("[APP] Echo Queued\r\n");
        }
    }
#endif
}

// [关键] 系统事件回调
void Adapter_OnEvent(LoRa_Event_t event, void *arg) {
    const LoRa_Config_t *cfg = Service_GetConfig();

    switch(event) {
        case LORA_EVENT_INIT_SUCCESS:
            Serial_Printf("[EVT] LoRa Init Done.\r\n");
            if (cfg) {
                Serial_Printf("      UUID: 0x%08X\r\n", cfg->uuid);
                Serial_Printf("      NetID: %d\r\n", cfg->net_id);
            }
            break;
            
        case LORA_EVENT_MSG_SENT:
            Serial_Printf("[EVT] TX Complete\r\n");
            break;
            
        case LORA_EVENT_MSG_RECEIVED:
            LED2_Turn();
            break;
            
        case LORA_EVENT_BIND_SUCCESS:
            Serial_Printf("[EVT] BIND Success! New ID: %d\r\n", *(uint16_t*)arg);
            break;
            
        default: break;
    }
}

// 结构体名称修正为 LoRa_Callback_t
const LoRa_Callback_t my_adapter = {
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

void Force_Init_Config(void) {
    LoRa_Config_t cfg;
    Flash_ReadLoRaConfig(&cfg);
    
    uint16_t target_id = (TEST_ROLE == 1) ? 1 : 2;
    uint16_t target_group = 100; 
    
    if (cfg.net_id != target_id || cfg.group_id != target_group || cfg.magic != LORA_CFG_MAGIC) {
        Serial_Printf("[TEST] Forcing Config...\r\n");
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
        Adapter_SystemReset();
    }
}

// ============================================================================
// 3. 主函数
// ============================================================================

int main(void)
{
    SysTick_Init();
    LED_Init();
    Serial_Init();
    Port_Init_STM32();
    
    Force_Init_Config();
    
    Serial_Printf("\r\n=== LoRaPlat V3.0 Async Fixed ===\r\n");
    Serial_Printf("Role: %s\r\n", (TEST_ROLE==1)?"HOST":"SLAVE");

    // 使用 LoRa_Service_Init
    LoRa_Service_Init(&my_adapter, 0); 

    uint32_t last_blink = 0;

    while (1)
    {
        // 使用 LoRa_Service_Run
        LoRa_Service_Run();

#if (TEST_ROLE == 1)
        if (Serial_RxFlag == 1)
        {
            char *input = Serial_RxPacket;
            int len = strlen(input);
            while(len > 0 && (input[len-1] == '\r' || input[len-1] == '\n')) input[--len] = '\0';

            if (len > 0) {
                Serial_Printf("[PC] Input: %s\r\n", input);
                
                if (strncmp(input, "CMD ", 4) == 0) {
                    int target_id;
                    char msg[64];
                    if (sscanf(input + 4, "%d %[^\n]", &target_id, msg) == 2) {
                        Serial_Printf(" -> Sending to %d: %s\r\n", target_id, msg);
                        // 使用 LoRa_Service_Send
                        if (!LoRa_Service_Send((uint8_t*)msg, strlen(msg), target_id)) {
                            Serial_Printf("[APP] Busy!\r\n");
                        }
                    }
                }
            }
            Serial_RxFlag = 0; 
        }
#endif

        if (GetTick() - last_blink > 50) { 
            last_blink = GetTick();
            LED1_Turn(); 
        }
    }
}
