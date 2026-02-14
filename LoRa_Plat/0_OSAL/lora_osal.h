#ifndef __LORA_OSAL_H
#define __LORA_OSAL_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdarg.h>
#include "LoRaPlatConfig.h" 

// ============================================================
//                    1. 接口定义
// ============================================================

typedef struct {
    // --- 核心服务 (必须提供) ---
    uint32_t (*GetTick)(void);          
    void     (*DelayMs)(uint32_t ms);   
    void     (*EnterCritical)(void);    
    void     (*ExitCritical)(void);     
    
    // --- 调试服务 (可选) ---
    // 仅当 LORA_DEBUG_PRINT=1 时有效
    void     (*Log)(const char *fmt, va_list args);
    // [恢复功能] 允许用户提供自定义 HexDump，若为 NULL 则使用默认实现
    void     (*LogHex)(const char *tag, const void *data, uint16_t len);
    
    // --- 内存服务 (可选) ---
    void*    (*Malloc)(uint32_t size);
    void     (*Free)(void* ptr);
    
} LoRa_OSAL_Interface_t;

// ============================================================
//                    2. 初始化 API
// ============================================================

bool LoRa_OSAL_Init(const LoRa_OSAL_Interface_t *impl);

// ============================================================
//                    3. 业务调用宏
// ============================================================

uint32_t _osal_get_tick(void);
void     _osal_delay_ms(uint32_t ms);
void     _osal_enter_critical(void);
void     _osal_exit_critical(void);
void*    _osal_malloc(uint32_t size);
void     _osal_free(void* ptr);

#define OSAL_GetTick()          _osal_get_tick()
#define OSAL_DelayMs(ms)        _osal_delay_ms(ms)
#define OSAL_EnterCritical()    _osal_enter_critical()
#define OSAL_ExitCritical()     _osal_exit_critical()
#define OSAL_Malloc(sz)         _osal_malloc(sz)
#define OSAL_Free(ptr)          _osal_free(ptr)

// --- 日志宏 (编译期优化) ---
#if (defined(LORA_DEBUG_PRINT) && LORA_DEBUG_PRINT == 1)
    void _osal_log_wrapper(const char *fmt, ...);
    void _osal_hexdump_wrapper(const char *tag, const void *data, uint16_t len);
    
    #define LORA_LOG(...)       _osal_log_wrapper(__VA_ARGS__)
    #define LORA_HEXDUMP(t,d,l) _osal_hexdump_wrapper(t,d,l)
#else
    #define LORA_LOG(...)       do {} while (0)
    #define LORA_HEXDUMP(t,d,l) do {} while (0)
#endif

#endif // __LORA_OSAL_H
