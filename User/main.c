#include "stm32f10x.h"
#include "Delay.h"
#include "Serial.h"
#include "LED.h"
#include "lora_app.h"
#include "lora_driver.h" 
#include "Flash.h"
#include "cJSON.h" // 需要解析 JSON
#include <string.h>
#include <stdio.h>

// ============================================================
//                    测试角色配置
// ============================================================
// 1: 主机 (Host) - ID: 0x0001 (与从机一致，透传互通)
// 2: 从机 (Slave) - ID: 0x0001
#define TEST_ROLE       2

// ============================================================
//                    主机本地调试配置
// ============================================================
#define HOST_FORCE_CH_10    0

volatile uint8_t g_TimeoutFlag; 

// 声明外部函数以便本地调用
extern void App_FactoryReset(void);

// [新增] 打印当前 LoRa 参数状态
static void Print_LoRa_Status(void) {
    printf("\r\n=== LoRa Device Status ===\r\n");
    printf("  Addr (HW/SW):  0x%04X\r\n", g_LoRaConfig_Current.addr);
    printf("  Channel:       %d (Freq: %d MHz)\r\n", g_LoRaConfig_Current.channel, 410 + g_LoRaConfig_Current.channel);
    printf("  Power:         %d (0=11dBm, 3=20dBm)\r\n", g_LoRaConfig_Current.power);
    printf("  Air Rate:      %d\r\n", g_LoRaConfig_Current.air_rate);
    printf("  Mode (TMODE):  %d (0=Transparent, 1=Fixed)\r\n", g_LoRaConfig_Current.tmode);
    printf("==========================\r\n");
}

// [新增] 处理本地配置指令
static void Handle_Local_Command(const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;

    cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
    if (cmd_item && strcmp(cmd_item->valuestring, "LOCAL_SET") == 0) {
        printf("[MAIN] Applying Local Config...\r\n");
        
        // 解析参数并更新全局配置
        cJSON *item;
        item = cJSON_GetObjectItem(root, "ch");
        if (item) g_LoRaConfig_Current.channel = (uint8_t)item->valuedouble;
        
        item = cJSON_GetObjectItem(root, "addr");
        if (item) g_LoRaConfig_Current.addr = (uint16_t)item->valuedouble;
        
        item = cJSON_GetObjectItem(root, "pwr");
        if (item) g_LoRaConfig_Current.power = (uint8_t)item->valuedouble;
        
        item = cJSON_GetObjectItem(root, "tmode");
        if (item) g_LoRaConfig_Current.tmode = (uint8_t)item->valuedouble;

        // 应用配置到模块
        if (Drv_ApplyConfig(&g_LoRaConfig_Current)) {
            // 打印完整信息
            Print_LoRa_Status();
        } else {
            printf("[MAIN] Config Failed!\r\n");
        }
    }
    cJSON_Delete(root);
}

int main(void)
{
    SysTick_Init();
    LED_Init();
    Serial_Init();
    
    for(int i=0; i<3; i++) { LED1_ON(); Delay_ms(100); LED1_OFF(); Delay_ms(100); }

    printf("\r\n=========================================\r\n");
    #if (TEST_ROLE == 1)
        printf("      [MODE] HOST (ID: 0x0001)           \r\n");
        uint16_t my_id = 0x0001;
    #else
        printf("      [MODE] SLAVE (ID: 0x0001)          \r\n");
        uint16_t my_id = 0x0001;
    #endif
    printf("=========================================\r\n");

    // 初始化 LoRa
    LoRa_App_Init(my_id);
    
    // [新增] 启动时打印一次状态
    Print_LoRa_Status();

    #if (TEST_ROLE == 1 && HOST_FORCE_CH_10 == 1)
        printf("[TEST] Host forcing local channel to 10...\r\n");
        g_LoRaConfig_Current.channel = 10; 
        Drv_ApplyConfig(&g_LoRaConfig_Current); 
        extern void Port_ClearRxBuffer(void);
        Port_ClearRxBuffer();
        Print_LoRa_Status();
    #endif

    printf("[SYS] Loop Start. Waiting for commands...\r\n");

    while (1)
    {
        LoRa_App_Task();

        #if (TEST_ROLE == 1)
        if (Serial_RxFlag == 1)
        {
            char *input_str = Serial_RxPacket;
            uint16_t len = strlen(input_str);
            while(len > 0 && (input_str[len-1] == '\r' || input_str[len-1] == '\n')) {
                input_str[--len] = '\0';
            }

            if (len > 0)
            {
                printf("[HOST] PC Input: %s\r\n", input_str);
                
                // [新增] 本地配置指令处理
                if (strstr(input_str, "LOCAL_SET")) {
                    Handle_Local_Command(input_str);
                }
                // 本地救砖指令
                else if (strcmp(input_str, "LOCAL_RESET") == 0) {
                    App_FactoryReset();
                }
                else {
                    // 默认发给从机 (ID: 0x0001)
                    printf("[HOST] Sending to 0x0001...\r\n");
                    LED1_ON();
                    Manager_SendPacket((uint8_t*)input_str, len, 0x0001);
                    LED1_OFF();
                }
            }
            Serial_RxFlag = 0;
        }
        #endif
        
        #if (TEST_ROLE == 2)
            static uint32_t last_tick = 0;
            if (GetTick() - last_tick > 2000) {
                LED1_ON(); Delay_ms(10); LED1_OFF();
                last_tick = GetTick();
            }
        #endif
    }
}
