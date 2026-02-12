#ifndef __LORA_PLAT_CONFIG_H
#define __LORA_PLAT_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 通用类型定义
// ============================================================
typedef enum {
    LORA_OK = 0,
    LORA_ERR_BUSY,
    LORA_ERR_PARAM,
    LORA_ERR_TIMEOUT,
    LORA_ERR_CRC,
    LORA_ERR_MEM,
    LORA_ERR_HARDWARE,
    LORA_ERR_AT_FAIL // [修复] 新增此错误码
} LoRa_Result_t;

// ============================================================
//                    2. 基础参数定义
// ============================================================
typedef enum {
    LORA_RATE_0K3 = 0, LORA_RATE_1K2, LORA_RATE_2K4,
    LORA_RATE_4K8, LORA_RATE_9K6, LORA_RATE_19K2
} LoRa_AirRate_t;

typedef enum {
    LORA_POWER_11dBm = 0, LORA_POWER_14dBm,
    LORA_POWER_17dBm, LORA_POWER_20dBm
} LoRa_Power_t;

typedef enum {
    LORA_TMODE_TRANSPARENT = 0, 
    LORA_TMODE_FIXED       = 1  
} LoRa_TMode_t;

// ============================================================
//                    3. 默认出厂配置
// ============================================================
#define DEFAULT_LORA_CHANNEL    23
#define DEFAULT_LORA_RATE       LORA_RATE_19K2
#define DEFAULT_LORA_POWER      LORA_POWER_11dBm
#define DEFAULT_LORA_TMODE      LORA_TMODE_TRANSPARENT
#define DEFAULT_LORA_TOKEN      0x00000000

#define LORA_ID_UNASSIGNED      0x0000
#define LORA_ID_BROADCAST       0xFFFF
#define LORA_HW_ADDR_DEFAULT    0x0000
#define LORA_GROUP_ID_DEFAULT   0x0000

// ============================================================
//                    4. 配置结构体
// ============================================================
#define LORA_CFG_MAGIC          0x5E            

typedef struct {
    uint8_t  magic;
    uint32_t uuid;
    uint16_t net_id;
    uint16_t group_id;
    uint32_t token;
    
    uint16_t hw_addr;
    uint8_t  channel;
    uint8_t  power;
    uint8_t  air_rate;
    uint8_t  tmode;
    
    uint8_t  padding[1];
} LoRa_Config_t;

// ============================================================
//                    5. 系统功能开关
// ============================================================
#define LORA_ENABLE_ACK         1   
#define LORA_ENABLE_CRC         1   
#define LORA_DEBUG_PRINT        1   

#define LORA_ACK_DELAY_MS       100  
#define LORA_TX_TIMEOUT_MS      1000 
#define LORA_ACK_TIMEOUT_MS     2000 
#define LORA_MAX_RETRY          3    

#endif
