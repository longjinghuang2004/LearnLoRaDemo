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
#include "stm32f10x.h"  // 用于开关中断
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

// 适配临界区 (关中断)
static void Demo_EnterCritical(void) {
    __disable_irq();
}

// 适配临界区 (开中断)
static void Demo_ExitCritical(void) {
    __enable_irq();
}

// 适配日志打印 (变参处理)
static void Demo_Log(const char *fmt, va_list args) {
    char buf[128];
    // 格式化字符串
    vsnprintf(buf, sizeof(buf), fmt, args);
    // 通过串口发送
    Serial_Printf("%s", buf);
}

// 适配 HexDump (可选，优化性能)
static void Demo_LogHex(const char *tag, const void *data, uint16_t len) {
    Serial_HexDump(tag, (const uint8_t*)data, len);
}

// ============================================================
//                    2. 接口注册结构体
// ============================================================

static const LoRa_OSAL_Interface_t s_OsalImpl = {
    .GetTick       = Demo_GetTick,
    .DelayMs       = Demo_DelayMs,
    .EnterCritical = Demo_EnterCritical,
    .ExitCritical  = Demo_ExitCritical,
    .Log           = Demo_Log,
    .LogHex        = Demo_LogHex, // 注册优化后的 HexDump
    .Malloc        = NULL,        // 裸机不使用动态内存
    .Free          = NULL
};

// ============================================================
//                    3. 公开初始化函数
// ============================================================

/**
 * @brief 在 main.c 中调用此函数以激活 OSAL
 */
void Demo_OSAL_Init(void) {
    // 将本地实现注入到 LoRaPlat 核心
    LoRa_OSAL_Init(&s_OsalImpl);
}
