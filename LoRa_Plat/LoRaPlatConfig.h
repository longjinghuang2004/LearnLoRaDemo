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

// 传输模式 (新增)
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
#define DEFAULT_LORA_ADDR       0x0000          // 默认硬件地址 0
#define DEFAULT_LORA_TOKEN      123456          // 默认安全令牌
#define DEFAULT_LORA_TMODE      LORA_TMODE_TRANSPARENT

// 软件ID默认值 (App层可覆盖)
#define LORA_LOCAL_ID           0x0001          

// ============================================================
//                    3. 配置结构体
// ============================================================
#define LORA_CFG_MAGIC          0x5B            // [修改] Magic变更为0x5B以强制刷新旧Flash

typedef struct {
    uint8_t  magic;             // 有效标志
    uint32_t token;             // 安全令牌
    uint16_t addr;              // 硬件地址 (兼软件ID)
    uint8_t  channel;           // 信道 (0-31)
    uint8_t  power;             // 功率 (0-3)
    uint8_t  air_rate;          // 空速 (0-5)
    uint8_t  tmode;             // [新增] 传输模式 (0=透传, 1=定向)
    uint8_t  padding;           // 对齐
} LoRa_Config_t;

// ============================================================
//                    4. 系统参数
// ============================================================
#define LORA_ENABLE_ACK         true
#define LORA_ENABLE_CRC         true
#define LORA_DEBUG_PRINT        1

#define LORA_ACK_DELAY_MS       50
#define LORA_TX_TIMEOUT_MS      1000
#define LORA_ACK_TIMEOUT_MS     2000
#define LORA_MAX_RETRY          3

#endif // __LORA_PLAT_CONFIG_H
