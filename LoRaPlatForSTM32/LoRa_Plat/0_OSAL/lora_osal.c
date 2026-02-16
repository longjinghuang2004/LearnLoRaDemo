/**
  ******************************************************************************
  * @file    lora_osal.c
  * @author  LoRaPlat Team
  * @brief   操作系统抽象层 (OSAL) 实现 V3.2.1 (Fix Warnings)
  ******************************************************************************
  */

#include "lora_osal.h"
#include <stdio.h> 

// [新增] 内部偏移量变量
static volatile uint32_t s_TickOffset = 0;

// ============================================================
//                    1. 默认桩函数 (Safety Stubs)
// ============================================================

static uint32_t Stub_GetTick(void) { return 0; }
static void     Stub_DelayMs(uint32_t ms) { (void)ms; }
static void     Stub_EnterCritical(void) {}
static void     Stub_ExitCritical(void) {}
static void*    Stub_Malloc(uint32_t size) { (void)size; return NULL; }
static void     Stub_Free(void* ptr) { (void)ptr; }
static void     Stub_LogHex(const char *tag, const void *data, uint16_t len) { (void)tag; (void)data; (void)len; }

static LoRa_OSAL_Interface_t s_Impl = {
    .GetTick       = Stub_GetTick,
    .DelayMs       = Stub_DelayMs,
    .EnterCritical = Stub_EnterCritical,
    .ExitCritical  = Stub_ExitCritical,
    .Log           = NULL, 
    .LogHex        = Stub_LogHex,
    .Malloc        = Stub_Malloc,
    .Free          = Stub_Free
};

static bool s_IsInit = false;

// ============================================================
//                    2. 初始化
// ============================================================

bool LoRa_OSAL_Init(const LoRa_OSAL_Interface_t *impl) {
    // 这里不能用 LORA_CHECK，因为 Log 可能还没初始化
    if (!impl) return false; 
    
    if (!impl->GetTick || !impl->DelayMs || !impl->EnterCritical || !impl->ExitCritical) {
        return false; 
    }
    
    s_Impl.GetTick       = impl->GetTick;
    s_Impl.DelayMs       = impl->DelayMs;
    s_Impl.EnterCritical = impl->EnterCritical;
    s_Impl.ExitCritical  = impl->ExitCritical;
    
    if (impl->Log)    s_Impl.Log    = impl->Log;
    if (impl->Malloc) s_Impl.Malloc = impl->Malloc;
    if (impl->Free)   s_Impl.Free   = impl->Free;
    
    // 智能 HexDump 注册
    if (impl->LogHex) {
        s_Impl.LogHex = impl->LogHex;
    } 
    
    s_IsInit = true;
    return true;
}

// ============================================================
//                    3. 核心包装器
// ============================================================

uint32_t _osal_get_tick(void) { return s_Impl.GetTick(); }
void     _osal_delay_ms(uint32_t ms) { s_Impl.DelayMs(ms); }
void     _osal_enter_critical(void) { s_Impl.EnterCritical(); }
void     _osal_exit_critical(void) { s_Impl.ExitCritical(); }
void*    _osal_malloc(uint32_t size) { return s_Impl.Malloc(size); }
void     _osal_free(void* ptr) { s_Impl.Free(ptr); }

// [新增] 补偿函数实现
void LoRa_OSAL_CompensateTick(uint32_t ms) {
    if (ms == 0) return;
    // 必须在临界区保护下修改，防止与 GetTick 竞争
    s_Impl.EnterCritical();
    s_TickOffset += ms;
    s_Impl.ExitCritical();
}

// ============================================================
//                    4. 日志包装器
// ============================================================

#if (defined(LORA_DEBUG_PRINT) && LORA_DEBUG_PRINT == 1)

void _osal_log_wrapper(const char *fmt, ...) {
    if (!s_IsInit || s_Impl.Log == NULL) return;
    
    va_list args;
    va_start(args, fmt);
    s_Impl.Log(fmt, args);
    va_end(args);
}

void _osal_hexdump_wrapper(const char *tag, const void *data, uint16_t len) {
    if (!s_IsInit) return;

    // 1. 优先使用用户提供的 HexDump
    if (s_Impl.LogHex != Stub_LogHex) {
        s_Impl.LogHex(tag, data, len);
        return;
    }

    // 2. 否则使用默认实现 (依赖 Log 接口)
    if (s_Impl.Log == NULL) return;

    const uint8_t *p = (const uint8_t*)data;
    char buf[64]; 
    
    _osal_log_wrapper("%s (Len=%d): ", tag, len);

    #define CHUNK_SIZE 16
    for (uint16_t i = 0; i < len; i += CHUNK_SIZE) {
        uint16_t chunk = (len - i < CHUNK_SIZE) ? (len - i) : CHUNK_SIZE;
        int pos = 0;
        for (uint16_t j = 0; j < chunk; j++) {
            pos += sprintf(buf + pos, "%02X ", p[i + j]);
        }
        _osal_log_wrapper("%s", buf);
    }
    _osal_log_wrapper("\r\n");
}

#endif
