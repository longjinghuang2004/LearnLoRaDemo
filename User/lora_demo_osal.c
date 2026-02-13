#include "lora_osal.h"
#include "Delay.h"      // 原有的延时驱动
#include "Serial.h"     // 原有的串口驱动
#include "stm32f10x.h"  // 用于关中断
#include <stdio.h>
#include <stdarg.h>

// --- 1. 适配 GetTick ---
static uint32_t Demo_GetTick(void) {
    return GetTick(); // 调用 System/Delay.h
}

// --- 2. 适配 DelayMs ---
static void Demo_DelayMs(uint32_t ms) {
    Delay_ms(ms); // 调用 System/Delay.h
}

// --- 3. 适配临界区 (裸机版) ---
static void Demo_EnterCritical(void) {
    __disable_irq(); // 关全局中断
}

static void Demo_ExitCritical(void) {
    __enable_irq(); // 开全局中断
}

// --- 4. 适配日志 (变参处理) ---
static void Demo_Log(const char *fmt, va_list args) {
    // Serial_Printf 不支持 va_list，我们需要先格式化到缓冲区
    // 注意：栈空间有限，缓冲区不要太大
    char buf[128]; 
    vsnprintf(buf, sizeof(buf), fmt, args);
    
    // 调用原有的串口发送
    // 注意：Serial_Printf 内部可能也有格式化，这里直接发字符串更高效
    // 但为了兼容性，我们假设 Serial_Printf("%s", buf) 是安全的
    Serial_Printf("%s", buf); 
}

// --- 5. 聚合接口 ---
static const LoRa_OSAL_Interface_t s_OsalImpl = {
    .GetTick       = Demo_GetTick,
    .DelayMs       = Demo_DelayMs,
    .EnterCritical = Demo_EnterCritical,
    .ExitCritical  = Demo_ExitCritical,
    .Log           = Demo_Log,
    .Malloc        = NULL, // 裸机暂不需要
    .Free          = NULL,
		.LogHex 			 = NULL
};

// --- 6. 公开初始化函数 ---
void Demo_OSAL_Init(void) {
    LoRa_OSAL_Init(&s_OsalImpl);
}
