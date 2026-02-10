
#include "stm32f10x.h"
#include "Delay.h"
#include "Serial.h"
#include "LED.h"
#include "lora_app.h"
// #include "lora_port.h" // 不需要了

volatile uint8_t g_TimeoutFlag;

int main(void)
{
    // 1. 基础硬件初始化
    SysTick_Init();
    LED_Init();
    Serial_Init();
    LED1_OFF();
    
    printf("\r\n=========================================\r\n");
    printf("      LoRa System Boot (Final)           \r\n");
    printf("=========================================\r\n");
    
    // 2. 业务层初始化 (内部会自动调用 Drv_SmartConfig)
    // 这一步会完成所有的 LoRa 配置和自检
    LoRa_App_Init();
    
    printf("[SYS] Entering Main Loop...\r\n");
    
    // 3. 主循环
    while (1)
    {
        LoRa_App_Task();
    }
}
