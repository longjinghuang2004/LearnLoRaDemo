#ifndef __LORA_PLAT_CONFIG_H
#define __LORA_PLAT_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 归一化参数定义 (通用语言)
// ============================================================

// 空中速率 (Air Data Rate)
typedef enum {
    LORA_RATE_0K3 = 0,      // 极低速 (最远距离)
    LORA_RATE_1K2,
    LORA_RATE_2K4,
    LORA_RATE_4K8,
    LORA_RATE_9K6,
    LORA_RATE_19K2          // 高速 (近距离)
} LoRa_AirRate_t;

// 发射功率 (Transmit Power)
typedef enum {
    LORA_POWER_11dBm = 0,   // 最低功耗 (桌面调试专用)
    LORA_POWER_14dBm,
    LORA_POWER_17dBm,
    LORA_POWER_20dBm        // 最大功率 (远距离专用)
} LoRa_Power_t;

// 串口波特率 (UART Baudrate)
typedef enum {
    LORA_BAUD_9600 = 0,
    LORA_BAUD_115200
} LoRa_BaudRate_t;

// ============================================================
//                    2. 默认出厂配置 (Smart Init 目标值)
// ============================================================

// 硬件参数
#define DEFAULT_LORA_CHANNEL    23              // 信道 (0-31) -> 433MHz
#define DEFAULT_LORA_RATE       LORA_RATE_19K2  // 默认高速
// [关键修改] 桌面调试必须用最低功率，防止接收端饱和
#define DEFAULT_LORA_POWER      LORA_POWER_11dBm 
#define DEFAULT_LORA_BAUD       LORA_BAUD_115200

// 协议参数
#define LORA_LOCAL_ID           0x0001          // 本机ID (App层可覆盖)
#define LORA_ENABLE_ACK         true            // 默认开启ACK
#define LORA_ENABLE_CRC         true            // 默认开启CRC

// 时序参数 (ms)
#define LORA_ACK_DELAY_MS       50              // [修改] 增加ACK延时，给主机切换RX留足时间
#define LORA_TX_TIMEOUT_MS      1000            // 发送超时
#define LORA_ACK_TIMEOUT_MS     2000            // 等待ACK超时
#define LORA_MAX_RETRY          3               // 最大重传次数

// ============================================================
//                    3. 功能开关
// ============================================================
#define LORA_DEBUG_PRINT        1               // 开启串口日志

#endif // __LORA_PLAT_CONFIG_H
