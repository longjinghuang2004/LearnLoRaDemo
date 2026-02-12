#ifndef __LORA_PLAT_CONFIG_H
#define __LORA_PLAT_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 基础参数定义
// ============================================================

// 空中速率
typedef enum {
    LORA_RATE_0K3 = 0, LORA_RATE_1K2, LORA_RATE_2K4,
    LORA_RATE_4K8, LORA_RATE_9K6, LORA_RATE_19K2
} LoRa_AirRate_t;

// 发射功率
typedef enum {
    LORA_POWER_11dBm = 0, LORA_POWER_14dBm,
    LORA_POWER_17dBm, LORA_POWER_20dBm
} LoRa_Power_t;

// 传输模式
typedef enum {
    LORA_TMODE_TRANSPARENT = 0, // 透传模式 (默认)
    LORA_TMODE_FIXED       = 1  // 定向模式
} LoRa_TMode_t;

// ============================================================
//                    2. 默认出厂配置
// ============================================================
#define DEFAULT_LORA_CHANNEL    23              // 默认信道 23 (433MHz)
#define DEFAULT_LORA_RATE       LORA_RATE_19K2  // 默认高速
#define DEFAULT_LORA_POWER      LORA_POWER_11dBm // 默认低功耗
#define DEFAULT_LORA_TMODE      LORA_TMODE_TRANSPARENT
#define DEFAULT_LORA_TOKEN      0x00000000      // 默认安全令牌

// 特殊 ID 定义
#define LORA_ID_UNASSIGNED      0x0000          // 未分配 ID (新设备默认)
#define LORA_ID_BROADCAST       0xFFFF          // 广播 ID
#define LORA_HW_ADDR_DEFAULT    0x0000          // 默认物理地址 (全通)
#define LORA_GROUP_ID_DEFAULT   0x0000          // 默认组 ID

// ============================================================
//                    3. 配置结构体
// ============================================================
// Magic变更为0x5E以强制刷新旧Flash结构 (V2.3 Update for Async FSM)
#define LORA_CFG_MAGIC          0x5E            

typedef struct {
    uint8_t  magic;             // 有效标志
    
    // --- 身份识别 (Identity) ---
    uint32_t uuid;              // 32位唯一标识
    uint16_t net_id;            // 逻辑 ID
    uint16_t group_id;          // 组 ID
    uint32_t token;             // 安全令牌
    
    // --- 硬件参数 (Physical) ---
    uint16_t hw_addr;           // 物理地址
    uint8_t  channel;           // 信道 (0-31)
    uint8_t  power;             // 功率 (0-3)
    uint8_t  air_rate;          // 空速 (0-5)
    uint8_t  tmode;             // 传输模式
    
    uint8_t  padding[1];        // 对齐保留
} LoRa_Config_t;

// ============================================================
//                    4. 系统参数配置 (System Parameters)
// ============================================================

#define LORA_ENABLE_ACK         true    // 开启 ACK 机制
#define LORA_ENABLE_CRC         true    // 开启 CRC 校验
#define LORA_ACK_DELAY_MS       300     // ACK 回复延时
#define LORA_TX_TIMEOUT_MS      1000    // 发送超时 (软件层)
#define LORA_ACK_TIMEOUT_MS     2000    // 等待 ACK 超时
#define LORA_MAX_RETRY          3       // 最大重传次数

// ============================================================
//                    5. 调试与错误处理 (Debug & Error)
// ============================================================

/**
 * @brief 调试日志开关
 * @note  1: 开启 (依赖 Serial.h)。 0: 关闭。
 */
#define LORA_DEBUG_PRINT        1 

/**
 * @brief 全局统一错误码
 */
typedef enum {
    LORA_OK = 0,
    
    // --- 同步错误 (调用即返回) ---
    LORA_ERR_BUSY,          // 驱动忙 (正在发送或配置)
    LORA_ERR_PARAM,         // 参数错误 (长度非法等)
    LORA_ERR_HARDWARE,      // 硬件故障 (GPIO/UART初始化失败)
    LORA_ERR_MEM_OVERFLOW,  // 内存溢出
    
    // --- 异步错误 (回调上报) ---
    LORA_ERR_TX_TIMEOUT,    // 发送超时 (AUX 迟迟不拉低，可能死机)
    LORA_ERR_RX_OVERFLOW,   // 接收缓冲区溢出
    LORA_ERR_AT_NO_RESP,    // AT指令无响应
    LORA_ERR_AT_ERROR,      // AT指令返回 Error
    
    // --- 协议层错误 ---
    LORA_ERR_ACK_TIMEOUT,   // 等待 ACK 超时
    LORA_ERR_CRC_FAIL       // CRC 校验失败
} LoRa_Result_t;

// 日志宏定义
#if LORA_DEBUG_PRINT
    #include "Serial.h" // 依赖现有的串口驱动
    // 使用 Serial_Printf (DMA非阻塞) 或 printf (重定向)
    #define LORA_LOG(fmt, ...)  Serial_Printf("[LORA] " fmt "\r\n", ##__VA_ARGS__)
#else
    #define LORA_LOG(fmt, ...)  ((void)0)
#endif

// ============================================================
//                    6. 全局变量声明 (Global Externs)
// ============================================================
// 让所有层都能看到当前配置，避免循环引用
extern LoRa_Config_t g_LoRaConfig_Current;

#endif // __LORA_PLAT_CONFIG_H
