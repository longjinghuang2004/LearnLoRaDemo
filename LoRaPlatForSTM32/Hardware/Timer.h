#ifndef __TIMER_H
#define __TIMER_H

#include "stm32f10x.h" // 引入头文件以识别 uint8_t 等

/* --- TIM2 for LED Blinking --- */
void Timer_Init(void);
void Timer_SetBlinkFreq(uint8_t Freq_Hz);


/* --- TIM3 for Packet Reception Timeout --- */
void Timeout_Timer_Init(void);
void Timeout_Timer_Start(void);
void Timeout_Timer_Stop(void);
void Timeout_Timer_Reset(void);


#endif
