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
    
    /**
     * @brief 进入临界区
     * @return 中断状态上下文 (STM32需要保存PRIMASK，ESP32返回0即可)
     */
    uint32_t (*EnterCritical)(void);    
    
    /**
     * @brief 退出临界区
     * @param ctx 进入时保存的中断状态上下文
     */
    void     (*ExitCritical)(uint32_t ctx);     
    
    // --- 调试服务 (可选) ---
    void     (*Log)(const char *fmt, va_list args);
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
//                    3. 业务调用宏 (增强版)
// ============================================================

// --- 核心函数原型声明 (解决 implicit declaration 警告) ---
uint32_t _osal_get_tick(void);
void     _osal_delay_ms(uint32_t ms);
void*    _osal_malloc(uint32_t size);
void     _osal_free(void* ptr);

// [关键修复] 显式声明临界区包装函数
uint32_t _osal_enter_critical(void);
void     _osal_exit_critical(uint32_t ctx);

// 补偿函数
void LoRa_OSAL_CompensateTick(uint32_t ms);

// --- 宏定义映射 ---
#define OSAL_GetTick()          _osal_get_tick()
#define OSAL_DelayMs(ms)        _osal_delay_ms(ms)
#define OSAL_Malloc(sz)         _osal_malloc(sz)
#define OSAL_Free(ptr)          _osal_free(ptr)

// [关键修复] 这里的宏现在调用的是有原型的函数
#define OSAL_EnterCritical()    _osal_enter_critical()
#define OSAL_ExitCritical(x)    _osal_exit_critical(x)
        
// --- 日志宏 ---
#if (defined(LORA_DEBUG_PRINT) && LORA_DEBUG_PRINT == 1)
    void _osal_log_wrapper(const char *fmt, ...);
    void _osal_hexdump_wrapper(const char *tag, const void *data, uint16_t len);
    
    #define LORA_LOG(...)       _osal_log_wrapper(__VA_ARGS__)
    #define LORA_HEXDUMP(t,d,l) _osal_hexdump_wrapper(t,d,l)
#else
    #define LORA_LOG(...)       do {} while (0)
    #define LORA_HEXDUMP(t,d,l) do {} while (0)
#endif

// --- 参数检查宏 ---
#if (defined(LORA_DEBUG_PRINT) && LORA_DEBUG_PRINT == 1)
    #define LORA_CHECK(expr, ret_val) \
        do { \
            if (!(expr)) { \
                LORA_LOG("[ERR] %s:%d Check Failed: "#expr"\r\n", __func__, __LINE__); \
                return ret_val; \
            } \
        } while(0)
        
    #define LORA_CHECK_VOID(expr) \
        do { \
            if (!(expr)) { \
                LORA_LOG("[ERR] %s:%d Check Failed: "#expr"\r\n", __func__, __LINE__); \
                return; \
            } \
        } while(0)
#else
    #define LORA_CHECK(expr, ret_val) \
        do { \
            if (!(expr)) { \
                return ret_val; \
            } \
        } while(0)

    #define LORA_CHECK_VOID(expr) \
        do { \
            if (!(expr)) { \
                return; \
            } \
        } while(0)
#endif

#endif // __LORA_OSAL_H
