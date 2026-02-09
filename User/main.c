



#include "lora_echo_test.h" // 引入新头文件

#include "stm32f10x.h"
#include "Delay.h"
#include "LED.h"
#include "Serial.h"

// 引入新的测试头文件 (不要引用 lora_test_task.h 或 lora_app_conf.h)
#include "lora_echo_test.h" 
volatile uint8_t g_TimeoutFlag;


int main(void)
{
    // 1. 基础系统初始化
    SysTick_Init(); 
    LED_Init();     
    Serial_Init();  
    
    printf("\r\n=========================================\r\n");
    printf("      STM32 LoRa Half-Duplex Echo Test    \r\n");
    printf("=========================================\r\n");
    
    // 2. 进入 LoRa 业务逻辑 (调用新函数)
    LoRa_Echo_Test_Run();
	
    return 0;
}
