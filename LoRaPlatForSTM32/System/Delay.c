#include "Delay.h"
#include <stdio.h>


static volatile uint32_t g_sysTickCount = 0;

/**
  * @brief  初始化SysTick定时器 (1ms中断)
  */
void SysTick_Init(void)
{
    // SystemCoreClock / 1000  => 1ms 中断一次
    if (SysTick_Config(SystemCoreClock / 1000))
    {
        // Capture error
        while (1);
    }
}

/**
  * @brief  SysTick中断服务函数调用，递增计数器
  * @note   需要在 stm32f10x_it.c 的 SysTick_Handler 中调用
  */
void SysTick_Increment(void)
{
    g_sysTickCount++;
}

/**
  * @brief  获取系统启动以来的毫秒数
  */
uint32_t GetTick(void)
{
    return g_sysTickCount;
}

/**
  * @brief  微秒级延时 (阻塞式，依然使用寄存器操作，保证精确)
  */
void Delay_us(uint32_t xus)
{
    uint32_t ticks;
    uint32_t told, tnow, tcnt = 0;
    uint32_t reload = SysTick->LOAD;
    
    ticks = xus * 72; // 72MHz主频
    told = SysTick->VAL;
    while (1)
    {
        tnow = SysTick->VAL;
        if (tnow != told)
        {
            if (tnow < told) tcnt += told - tnow;
            else tcnt += reload - tnow + told;
            told = tnow;
            if (tcnt >= ticks) break;
        }
    }
}

/**
  * @brief  毫秒级延时 (基于GetTick的阻塞延时)
  */
void Delay_ms(uint32_t xms)
{
    uint32_t start = GetTick();
    while (GetTick() - start < xms);
}
