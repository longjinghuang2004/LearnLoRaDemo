



#include "stm32f10x.h"
#include "Delay.h"
#include "LED.h"
#include "Serial.h"

volatile uint8_t g_TimeoutFlag;
void LoRa_Dual_Test_Run(void); 

int main(void)
{
    // 基础硬件初始化
    SysTick_Init(); // [新增]
    LED_Init();
    Serial_Init(); 
    Delay_ms(100);

    // 进入测试
    LoRa_Dual_Test_Run();
   
   return 0;
}

