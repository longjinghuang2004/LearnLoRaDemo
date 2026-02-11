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
#define LORA_GROUP_ID_DEFAULT   0x0000          // 默认组 ID (0表示无分组或默认组)

// ============================================================
//                    3. 配置结构体
// ============================================================
// [修改] Magic变更为0x5D以强制刷新旧Flash结构 (V2.2 Update)
#define LORA_CFG_MAGIC          0x5D            

typedef struct {
    uint8_t  magic;             // 有效标志
    
    // --- 身份识别 (Identity) ---
    uint32_t uuid;              // 32位唯一标识 (随机生成，终身不变)
    uint16_t net_id;            // 逻辑 ID (用于业务通信)
    uint16_t group_id;          // [新增] 组 ID (用于组播/逻辑分组)
    uint32_t token;             // 安全令牌 (可选)
    
    // --- 硬件参数 (Physical) ---
    uint16_t hw_addr;           // 物理地址 (写入模块AT+ADDR，通常为0)
    uint8_t  channel;           // 信道 (0-31)
    uint8_t  power;             // 功率 (0-3)
    uint8_t  air_rate;          // 空速 (0-5)
    uint8_t  tmode;             // 传输模式 (0=透传, 1=定向)
    
    uint8_t  padding[1];        // 对齐保留 (调整padding以保持4字节对齐)
} LoRa_Config_t;

// ============================================================
//                    4. 系统参数
// ============================================================
#define LORA_ENABLE_ACK         true
#define LORA_ENABLE_CRC         true
#define LORA_DEBUG_PRINT        1  // 默认开启调试日志

#define LORA_ACK_DELAY_MS       100
#define LORA_TX_TIMEOUT_MS      1000
#define LORA_ACK_TIMEOUT_MS     2000
#define LORA_MAX_RETRY          3

#endif // __LORA_PLAT_CONFIG_H
