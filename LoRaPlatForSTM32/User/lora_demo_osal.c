/**
  ******************************************************************************
  * @file    lora_demo_osal.c
  * @author  LoRaPlat Team
  * @brief   OSAL 接口适配层 (STM32F103 裸机版)
  ******************************************************************************
  */

#include "lora_osal.h"
#include "Delay.h"      // 硬件延时实现
#include "Serial.h"     // 硬件串口实现
#include "stm32f10x.h"  // [关键] 引入 STM32 核心头文件以支持 __get_PRIMASK
#include <stdio.h>
#include <stdarg.h>

// ============================================================
//                    1. 接口适配实现
// ============================================================

// 适配 GetTick
static uint32_t Demo_GetTick(void) {
    return GetTick(); // 来自 System/Delay.c
}

// 适配 DelayMs
static void Demo_DelayMs(uint32_t ms) {
    Delay_ms(ms); // 来自 System/Delay.c
}

// 适配日志打印 (变参处理)
static void Demo_Log(const char *fmt, va_list args) {
    char buf[128];
    vsnprintf(buf, sizeof(buf), fmt, args);
    Serial_Printf("%s", buf);
}

// 适配 HexDump
static void Demo_LogHex(const char *tag, const void *data, uint16_t len) {
    Serial_HexDump(tag, (const uint8_t*)data, len);
}

// [关键修复] 适配临界区 (关中断并保存状态)
// 签名必须是: uint32_t (*)(void)
static uint32_t Demo_EnterCritical(void) {
    uint32_t primask = __get_PRIMASK();
    __disable_irq();
    return primask;
}

// [关键修复] 适配临界区 (恢复状态)
// 签名必须是: void (*)(uint32_t)
static void Demo_ExitCritical(uint32_t ctx) {
    __set_PRIMASK(ctx);
}

// ============================================================
//                    2. 接口注册结构体
// ============================================================

static const LoRa_OSAL_Interface_t s_OsalImpl = {
    .GetTick       = Demo_GetTick,
    .DelayMs       = Demo_DelayMs,
    .EnterCritical = Demo_EnterCritical, // 现在类型完全匹配了
    .ExitCritical  = Demo_ExitCritical,  // 现在类型完全匹配了
    .Log           = Demo_Log,
    .LogHex        = Demo_LogHex, 
    .Malloc        = NULL,        
    .Free          = NULL
};

// ============================================================
//                    3. 公开初始化函数
// ============================================================

void Demo_OSAL_Init(void) {
    LoRa_OSAL_Init(&s_OsalImpl);
}
