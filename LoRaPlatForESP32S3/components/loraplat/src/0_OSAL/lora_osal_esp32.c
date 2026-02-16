/**
  ******************************************************************************
  * @file    lora_osal_esp32.c
  * @author  LoRaPlat Team
  * @brief   OSAL 接口适配层 (ESP32-S3 FreeRTOS 版)
  ******************************************************************************
  */

#include "lora_osal.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_log.h"
#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h> // for malloc/free

static const char *TAG = "LoRa_OSAL";

// ESP32 是双核系统，必须使用自旋锁保护临界区
static portMUX_TYPE s_lora_spinlock = portMUX_INITIALIZER_UNLOCKED;

// ============================================================
//                    1. 接口适配实现
// ============================================================

// 适配 GetTick (ms)
static uint32_t ESP32_GetTick(void) {
    // esp_timer_get_time() 返回微秒，除以 1000 转毫秒
    return (uint32_t)(esp_timer_get_time() / 1000ULL);
}

// 适配 DelayMs
static void ESP32_DelayMs(uint32_t ms) {
    // 挂起当前任务，释放 CPU
    vTaskDelay(pdMS_TO_TICKS(ms));
}

// [关键] 适配 EnterCritical
// 签名必须是: uint32_t (*)(void)
static uint32_t ESP32_EnterCritical(void) {
    // 进入临界区 (关中断 + 自旋锁)
    // FreeRTOS 内部支持嵌套调用，不需要我们手动保存状态
    taskENTER_CRITICAL(&s_lora_spinlock);
    
    // 返回 0 即可，因为 ESP32 不需要像 STM32 那样保存 PRIMASK
    return 0; 
}

// [关键] 适配 ExitCritical
// 签名必须是: void (*)(uint32_t)
static void ESP32_ExitCritical(uint32_t ctx) {
    (void)ctx; // 忽略参数，因为 FreeRTOS 不需要恢复外部保存的状态
    
    // 退出临界区
    taskEXIT_CRITICAL(&s_lora_spinlock);
}

// 适配日志打印
static void ESP32_Log(const char *fmt, va_list args) {
    // 使用临时缓冲区格式化字符串
    char buf[256];
    vsnprintf(buf, sizeof(buf), fmt, args);
    
    // 移除末尾可能的换行符，因为 ESP_LOGI 会自动添加换行
    size_t len = strlen(buf);
    if (len > 0 && buf[len - 1] == '\n') {
        buf[len - 1] = '\0';
    }
    
    ESP_LOGI(TAG, "%s", buf);
}

// 适配 HexDump
static void ESP32_LogHex(const char *tag, const void *data, uint16_t len) {
    ESP_LOG_BUFFER_HEX(tag, data, len);
}

// 适配 Malloc
static void* ESP32_Malloc(uint32_t size) {
    return malloc(size);
}

// 适配 Free
static void ESP32_Free(void* ptr) {
    free(ptr);
}

// ============================================================
//                    2. 接口注册结构体
// ============================================================

static const LoRa_OSAL_Interface_t s_OsalImpl = {
    .GetTick       = ESP32_GetTick,
    .DelayMs       = ESP32_DelayMs,
    .EnterCritical = ESP32_EnterCritical, // 类型完全匹配
    .ExitCritical  = ESP32_ExitCritical,  // 类型完全匹配
    .Log           = ESP32_Log,
    .LogHex        = ESP32_LogHex,
    .Malloc        = ESP32_Malloc,
    .Free          = ESP32_Free
};

// ============================================================
//                    3. 公开初始化函数
// ============================================================

// 在 main.c 中调用此函数
void LoRa_OSAL_Init_ESP32(void) {
    LoRa_OSAL_Init(&s_OsalImpl);
}
