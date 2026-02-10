#include "stm32f10x.h"
#include "Delay.h"
#include "Serial.h"
#include "LED.h"
#include "lora_app.h"
#include "lora_driver.h" 
#include "Flash.h"
#include <string.h>
#include <stdio.h>

// ============================================================
//                    测试角色配置
// ============================================================
// 1: 主机 (Host) - ID: 0x0002
// 2: 从机 (Slave) - ID: 0x0001
#define TEST_ROLE       1

// ============================================================
//                    主机本地调试配置
// ============================================================
#define HOST_FORCE_CH_10    0

volatile uint8_t g_TimeoutFlag; 

int main(void)
{
    SysTick_Init();
    LED_Init();
    Serial_Init();
    
    for(int i=0; i<3; i++) { LED1_ON(); Delay_ms(100); LED1_OFF(); Delay_ms(100); }

    printf("\r\n=========================================\r\n");
    #if (TEST_ROLE == 1)
        printf("      [MODE] HOST (ID: 0x0002)           \r\n");
        uint16_t my_id = 0x0002;
    #else
        printf("      [MODE] SLAVE (ID: 0x0001)          \r\n");
        uint16_t my_id = 0x0001;
    #endif
    printf("=========================================\r\n");

    // 初始化 LoRa，传入角色 ID
    LoRa_App_Init(my_id);

    #if (TEST_ROLE == 1 && HOST_FORCE_CH_10 == 1)
        printf("[TEST] Host forcing local channel to 10...\r\n");
        LoRa_Config_t temp_cfg;
        Flash_ReadLoRaConfig(&temp_cfg);
        temp_cfg.channel = 10; 
        Drv_ApplyConfig(&temp_cfg); 
        // 再次清空，防止 Apply 产生垃圾
        extern void Port_ClearRxBuffer(void);
        Port_ClearRxBuffer();
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
                
                // 默认发给从机 (ID: 0x0001)
                // 如果你想广播，改用 0xFFFF
                printf("[HOST] Sending to 0x0001...\r\n");
                LED1_ON();
                Manager_SendPacket((uint8_t*)input_str, len, 0x0001);
                LED1_OFF();
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
