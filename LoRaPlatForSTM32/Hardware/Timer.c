#include "stm32f10x.h"
#include "LED.h"

// 声明一个来自main.c的全局超时标志
extern volatile uint8_t g_TimeoutFlag;


/******************************************************************************/
/*                                TIM2 for LED                                */
/******************************************************************************/

// Timer_Init, Timer_SetBlinkFreq, TIM2_IRQHandler 函数保持不变
void Timer_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM2, ENABLE);
    
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_Period = 1000 - 1;
    TIM_TimeBaseInitStructure.TIM_Prescaler = 7200 - 1;
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM2, &TIM_TimeBaseInitStructure);
    
    TIM_ClearFlag(TIM2, TIM_FLAG_Update);
    TIM_ITConfig(TIM2, TIM_IT_Update, ENABLE);
    
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_Init(&NVIC_InitStructure);
    
    TIM_Cmd(TIM2, ENABLE);
}

void Timer_SetBlinkFreq(uint8_t Freq_Hz)
{
    if (Freq_Hz == 0) return;
    uint16_t arr_value = (10000 / (2 * Freq_Hz)) - 1;
    TIM_SetAutoreload(TIM2, arr_value);
    TIM_SetCounter(TIM2, 0);
}

void TIM2_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM2, TIM_IT_Update) == SET)
    {
        LED1_Turn();
        TIM_ClearITPendingBit(TIM2, TIM_IT_Update);
    }
}


/******************************************************************************/
/*                         TIM3 for Reception Timeout                         */
/******************************************************************************/

/**
  * @brief  [最终修正] 初始化TIM3作为5秒超时定时器
  * @note   假定TIM3时钟为72MHz。
  *         PSC = 7200 - 1  => 计数频率 = 72MHz / 7200 = 10kHz (0.1ms/tick)
  *         ARR = 50000 - 1 => 中断周期 = 50000 * 0.1ms = 5000ms = 5s
  */
void Timeout_Timer_Init(void)
{
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_TIM3, ENABLE);
    
    TIM_TimeBaseInitTypeDef TIM_TimeBaseInitStructure;
    TIM_TimeBaseInitStructure.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInitStructure.TIM_CounterMode = TIM_CounterMode_Up;
    TIM_TimeBaseInitStructure.TIM_Period = 50000 - 1;   // 50,000 ticks
    TIM_TimeBaseInitStructure.TIM_Prescaler = 7200 - 1;   // 10kHz tick frequency
    TIM_TimeBaseInitStructure.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(TIM3, &TIM_TimeBaseInitStructure);
    
    TIM_ClearFlag(TIM3, TIM_FLAG_Update);
    TIM_ITConfig(TIM3, TIM_IT_Update, ENABLE);
    
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = TIM3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 2;
    NVIC_Init(&NVIC_InitStructure);
}

// Timeout_Timer_Start, Stop, Reset, TIM3_IRQHandler 函数保持不变
void Timeout_Timer_Start(void)
{
    TIM_SetCounter(TIM3, 0);
    TIM_Cmd(TIM3, ENABLE);
}

void Timeout_Timer_Stop(void)
{
    TIM_Cmd(TIM3, DISABLE);
}

void Timeout_Timer_Reset(void)
{
    TIM_SetCounter(TIM3, 0);
}

void TIM3_IRQHandler(void)
{
    if (TIM_GetITStatus(TIM3, TIM_IT_Update) == SET)
    {
        Timeout_Timer_Stop();
        g_TimeoutFlag = 1;
        TIM_ClearITPendingBit(TIM3, TIM_IT_Update);
    }
}
