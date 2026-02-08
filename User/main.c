#include "stm32f10x.h"
#include "Delay.h"
#include "LED.h"
#include "Serial.h"
// #include "Model.h" ... 其他头文件暂时不用

volatile uint8_t g_TimeoutFlag;


// 引入测试函数的声明
void LoRa_Test_Run(void); 

int main(void)
{
    // 基础硬件初始化
    LED_Init();
    Serial_Init(); // 初始化 USART1 用于 printf
    Delay_ms(100);

    // --- 进入 LoRa 驱动测试模式 ---
    // 验证通过后，注释掉这行，恢复下方的主逻辑
    LoRa_Test_Run(); 

    /* 
    // 原有主逻辑 (暂时屏蔽)
    Model_Init();
    Timeout_Timer_Init();
    // ...
    while (1) { ... }
    */
   
   return 0;
}
