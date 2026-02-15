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
//                    3. 业务调用宏 (增强版)
// ============================================================

uint32_t _osal_get_tick(void);
void     _osal_delay_ms(uint32_t ms);
void*    _osal_malloc(uint32_t size);
void     _osal_free(void* ptr);


// [新增] 低功耗补偿接口
// 当系统从停止/待机模式唤醒后（SysTick已暂停），调用此函数补偿休眠时长
void LoRa_OSAL_CompensateTick(uint32_t ms);

// [新增] 临界区嵌套支持 (Cortex-M3/M4)
// 使用 __get_PRIMASK() 保存当前中断状态，__set_PRIMASK() 恢复
// 这样即使在 ISR 中调用，也不会意外开启全局中断
#if defined(__CC_ARM) || defined(__GNUC__)
    #include "stm32f10x.h" // 引入 CMSIS 核心头文件
    
    static inline uint32_t OSAL_EnterCritical(void) {
        uint32_t primask = __get_PRIMASK();
        __disable_irq();
        return primask;
    }
    
    static inline void OSAL_ExitCritical(uint32_t primask) {
        __set_PRIMASK(primask);
    }
#else
    // 非 ARM 编译器回退方案 (不支持嵌套)
    #define OSAL_EnterCritical()    __disable_irq()
    #define OSAL_ExitCritical(x)    __enable_irq()
#endif

#define OSAL_GetTick()          _osal_get_tick()
#define OSAL_DelayMs(ms)        _osal_delay_ms(ms)
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

// [新增] 参数检查宏
// 如果 expr 为假 (例如指针为空)，则打印错误日志并返回 ret_val
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
    // Release 模式下只做检查，不打印，不包含文件名字符串 (省空间)
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
