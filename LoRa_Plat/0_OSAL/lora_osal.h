/**
  ******************************************************************************
  * @file    lora_osal.h
  * @author  LoRaPlat Team
  * @brief   操作系统抽象层 (OSAL) 接口定义
  *          提供时间、延时、临界区、日志和内存管理的统一接口，
  *          实现协议栈与具体 OS/硬件 的解耦。
  ******************************************************************************
  */

#ifndef __LORA_OSAL_H
#define __LORA_OSAL_H

#include <stdint.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>

// 引入配置以获取 LORA_DEBUG_PRINT 宏
#include "LoRaPlatConfig.h" 

// ============================================================
//                    1. 接口定义 (Interface Definition)
// ============================================================

/**
 * @brief OSAL 接口结构体
 * @note  用户需要在 App 层实现这些函数，并在初始化时注册进来。
 */
typedef struct {
    // --- 核心服务 (必须实现) ---
    
    /**
     * @brief 获取系统运行时间 (毫秒)
     * @return 当前系统 Tick
     */
    uint32_t (*GetTick)(void);
    
    /**
     * @brief 毫秒级延时
     * @param ms 延时时间
     * @note  在裸机中通常是阻塞延时；在 RTOS 中应调用 vTaskDelay 等挂起函数。
     */
    void     (*DelayMs)(uint32_t ms);
    
    // --- 临界区保护 (必须实现) ---
    
    /**
     * @brief 进入临界区
     * @note  禁止中断或锁定互斥量，保护全局变量/硬件寄存器不被并发访问破坏。
     */
    void     (*EnterCritical)(void);
    
    /**
     * @brief 退出临界区
     */
    void     (*ExitCritical)(void);
    
    // --- 调试日志 (可选, 可为NULL) ---
    
    /**
     * @brief 格式化日志输出
     * @param fmt 格式字符串
     * @param args 变参列表
     * @note  建议在内部调用 vprintf 或 vsnprintf。
     */
    void     (*Log)(const char *fmt, va_list args);
    
    // --- 内存管理 (可选, 可为NULL) ---
    // 目前 V3.1 协议栈主要使用静态内存，这两个接口预留给未来扩展 (如组网层动态包管理)
    
    void*    (*Malloc)(uint32_t size);
    void     (*Free)(void* ptr);
		
		// 默认实现会调用 Log 接口分批打印
    void     (*LogHex)(const char *tag, const void *data, uint16_t len);
    
} LoRa_OSAL_Interface_t;

// ============================================================
//                    2. 初始化 API (Initialization)
// ============================================================

/**
 * @brief  初始化 OSAL 层
 * @param  impl: 指向用户实现的接口结构体
 * @return true=成功, false=核心接口缺失(GetTick/Delay/Critical为空)
 * @note   必须在调用任何其他 LoRa 服务之前调用此函数。
 */
bool LoRa_OSAL_Init(const LoRa_OSAL_Interface_t *impl);

// ============================================================
//                    3. 业务调用宏 (Macros for Usage)
// ============================================================
// 协议栈内部代码应直接调用这些宏，而不是直接操作函数指针

#define OSAL_GetTick()          _osal_get_tick()
#define OSAL_DelayMs(ms)        _osal_delay_ms(ms)
#define OSAL_EnterCritical()    _osal_enter_critical()
#define OSAL_ExitCritical()     _osal_exit_critical()
#define OSAL_Malloc(sz)         _osal_malloc(sz)
#define OSAL_Free(ptr)          _osal_free(ptr)

// 日志宏 (带编译开关控制)
// 当 LORA_DEBUG_PRINT 为 0 时，整行代码会被编译器优化掉，零开销。
#if (defined(LORA_DEBUG_PRINT) && LORA_DEBUG_PRINT == 1)
    void _osal_log_wrapper(const char *fmt, ...);
		#define LORA_LOG(...)       _osal_log_wrapper(__VA_ARGS__)

    // [修改] HexDump 宏直接调用包装器
    void _osal_hexdump_wrapper(const char *tag, const void *data, uint16_t len);
    #define LORA_HEXDUMP(t,d,l) _osal_hexdump_wrapper(t,d,l)
#else
    #define LORA_LOG(...)       ((void)0)
    #define LORA_HEXDUMP(t,d,l) ((void)0)
#endif

// ============================================================
//                    4. 内部函数声明 (Internal Functions)
// ============================================================
// 这些函数由 lora_osal.c 实现，通常不直接供用户调用

uint32_t _osal_get_tick(void);
void     _osal_delay_ms(uint32_t ms);
void     _osal_enter_critical(void);
void     _osal_exit_critical(void);
void*    _osal_malloc(uint32_t size);
void     _osal_free(void* ptr);

#endif // __LORA_OSAL_H
