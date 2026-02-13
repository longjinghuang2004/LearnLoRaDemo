/**
  ******************************************************************************
  * @file    lora_osal.c
  * @author  LoRaPlat Team
  * @brief   操作系统抽象层 (OSAL) 实现
  *          包含单例管理、默认桩函数(Stubs)和防御性检查逻辑。
  ******************************************************************************
  */

#include "lora_osal.h"
#include <stddef.h>
#include <stdio.h>  // 用于 sprintf (仅在默认 HexDump 中使用)

// ============================================================
//                    1. 默认桩函数 (Default Stubs)
// ============================================================
// 当用户未注册某些可选接口，或初始化前意外调用时，
// 这些“哑巴”函数能防止程序崩溃。

static uint32_t Stub_GetTick(void) { return 0; }
static void     Stub_DelayMs(uint32_t ms) { (void)ms; }
static void     Stub_EnterCritical(void) {}
static void     Stub_ExitCritical(void) {}
static void     Stub_Log(const char *fmt, va_list args) { (void)fmt; (void)args; }
static void*    Stub_Malloc(uint32_t size) { (void)size; return NULL; }
static void     Stub_Free(void* ptr) { (void)ptr; }

// 默认的 HexDump 实现 (桩)
// 如果用户没注册 LogHex，且没注册 Log，就用这个空的
static void     Stub_LogHex(const char *tag, const void *data, uint16_t len) {
    (void)tag; (void)data; (void)len;
}

// ============================================================
//                    2. 单例实例 (Singleton Instance)
// ============================================================

static LoRa_OSAL_Interface_t s_Impl = {
    .GetTick       = Stub_GetTick,
    .DelayMs       = Stub_DelayMs,
    .EnterCritical = Stub_EnterCritical,
    .ExitCritical  = Stub_ExitCritical,
    .Log           = Stub_Log,
    .LogHex        = Stub_LogHex, // 初始指向空桩
    .Malloc        = Stub_Malloc,
    .Free          = Stub_Free
};

static bool s_IsInit = false;

// ============================================================
//                    3. 内部辅助：默认 HexDump 实现
// ============================================================

#if (defined(LORA_DEBUG_PRINT) && LORA_DEBUG_PRINT == 1)
// 这是一个“软实现”，它利用已有的 Log 接口来打印 Hex。
// 优点：用户不需要额外适配。
// 缺点：依赖 sprintf，效率不如直接操作 UART 寄存器。
static void Default_LogHex_Impl(const char *tag, const void *data, uint16_t len) {
    // 如果底层 Log 也是桩函数，那打印也没意义，直接返回
    if (s_Impl.Log == Stub_Log) return;

    const uint8_t *p = (const uint8_t*)data;
    char buf[64]; // 临时缓冲区，用于格式化一行 Hex
    
    // 1. 打印标签 (复用 Log 包装器)
    _osal_log_wrapper("%s (Len=%d): ", tag, len);

    // 2. 分批打印 (每行 16 字节)
    #define CHUNK_SIZE 16
    for (uint16_t i = 0; i < len; i += CHUNK_SIZE) {
        uint16_t chunk = (len - i < CHUNK_SIZE) ? (len - i) : CHUNK_SIZE;
        int pos = 0;
        
        // 格式化为 "01 02 03 ..."
        for (uint16_t j = 0; j < chunk; j++) {
            pos += sprintf(buf + pos, "%02X ", p[i + j]);
        }
        
        // 输出这一行
        _osal_log_wrapper("%s", buf);
    }
    _osal_log_wrapper("\r\n");
}
#endif

// ============================================================
//                    4. 初始化与防御 (Init & Defense)
// ============================================================

bool LoRa_OSAL_Init(const LoRa_OSAL_Interface_t *impl) {
    if (impl == NULL) return false;
    
    // [防御策略 1] 核心接口检查
    if (!impl->GetTick || !impl->DelayMs || !impl->EnterCritical || !impl->ExitCritical) {
        return false; 
    }
    
    // [防御策略 2] 注册核心接口
    s_Impl.GetTick       = impl->GetTick;
    s_Impl.DelayMs       = impl->DelayMs;
    s_Impl.EnterCritical = impl->EnterCritical;
    s_Impl.ExitCritical  = impl->ExitCritical;
    
    // [防御策略 3] 注册可选接口
    if (impl->Log)    s_Impl.Log    = impl->Log;
    if (impl->Malloc) s_Impl.Malloc = impl->Malloc;
    if (impl->Free)   s_Impl.Free   = impl->Free;
    
    // [防御策略 4] 智能 HexDump 注册
    if (impl->LogHex) {
        // 用户提供了高效实现，直接用用户的
        s_Impl.LogHex = impl->LogHex;
    } else {
        // 用户没提供，但在开启调试时，我们尝试使用默认实现
        #if (defined(LORA_DEBUG_PRINT) && LORA_DEBUG_PRINT == 1)
            if (s_Impl.Log != Stub_Log) {
                s_Impl.LogHex = Default_LogHex_Impl;
            }
        #endif
    }
    
    s_IsInit = true;
    return true;
}

// ============================================================
//                    5. 包装函数实现 (Wrappers)
// ============================================================

uint32_t _osal_get_tick(void) {
    return s_Impl.GetTick();
}

void _osal_delay_ms(uint32_t ms) {
    s_Impl.DelayMs(ms);
}

void _osal_enter_critical(void) {
    s_Impl.EnterCritical();
}

void _osal_exit_critical(void) {
    s_Impl.ExitCritical();
}

void* _osal_malloc(uint32_t size) {
    return s_Impl.Malloc(size);
}

void _osal_free(void* ptr) {
    s_Impl.Free(ptr);
}

// 日志包装器
#if (defined(LORA_DEBUG_PRINT) && LORA_DEBUG_PRINT == 1)

void _osal_log_wrapper(const char *fmt, ...) {
    if (!s_IsInit) return;
    
    va_list args;
    va_start(args, fmt);
    s_Impl.Log(fmt, args);
    va_end(args);
}

void _osal_hexdump_wrapper(const char *tag, const void *data, uint16_t len) {
    if (!s_IsInit) return;
    s_Impl.LogHex(tag, data, len);
}

#endif
