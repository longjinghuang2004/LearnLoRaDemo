/**
  ******************************************************************************
  * @file    mod_lora.h
  * @author  LoRaPlat Team
  * @brief   Layer 2: LoRa 核心驱动层 (中间件) - V2.1 (支持动态协议配置)
  ******************************************************************************
  */

#ifndef __MOD_LORA_H
#define __MOD_LORA_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ========================================================================== */
/*                                 配置宏定义                                  */
/* ========================================================================== */

// 默认协议定义 (初始化时使用)
#define LORA_DEFAULT_HEAD_0     'C'
#define LORA_DEFAULT_HEAD_1     'M'
#define LORA_DEFAULT_TAIL_0     '\r'
#define LORA_DEFAULT_TAIL_1     '\n'

// 内部接收缓冲区大小 (最大单包长度)
#define LORA_MAX_PACKET_SIZE    256

// AT指令响应超时时间 (ms)
#define LORA_AT_TIMEOUT         500

/* ========================================================================== */
/*                                 枚举定义                                    */
/* ========================================================================== */

// 发射功率 (AT+TPOWER)
typedef enum {
    LORA_PWR_11DBM = 0,
    LORA_PWR_14DBM = 1,
    LORA_PWR_17DBM = 2,
    LORA_PWR_20DBM = 3  // Default
} LoRa_Power_e;

// 空中速率 (AT+WLRATE)
typedef enum {
    LORA_RATE_0K3  = 0,
    LORA_RATE_1K2  = 1,
    LORA_RATE_2K4  = 2,
    LORA_RATE_4K8  = 3,
    LORA_RATE_9K6  = 4,
    LORA_RATE_19K2 = 5  // Default
} LoRa_Rate_e;

// 内部接收状态机
typedef enum {
    RX_STATE_WAIT_HEAD_0,   // 等待 Head[0]
    RX_STATE_WAIT_HEAD_1,   // 等待 Head[1]
    RX_STATE_RECEIVING,     // 接收 Payload
    RX_STATE_WAIT_TAIL_1    // 等待 Tail[1] (收到 Tail[0] 后)
} LoRa_RxState_e;

/* ========================================================================== */
/*                                 接口契约                                    */
/* ========================================================================== */

/**
 * @brief 上层数据接收回调类型 (向上通知)
 * @param payload 指向有效载荷的指针 (不含头尾)
 * @param length  有效载荷长度
 */
typedef void (*LoRa_RxHandler_t)(const uint8_t *payload, uint16_t length);

/**
 * @brief 底层硬件操作接口 (向下调用)
 */
typedef struct {
    uint16_t (*Write)(const uint8_t *data, uint16_t len);
    uint16_t (*Read)(uint8_t *buf, uint16_t max_len);
    void (*SetReset)(uint8_t level);
    void (*SetModePin)(uint8_t level);
    uint8_t (*GetAux)(void);
    void (*DelayMs)(uint32_t ms);
    uint32_t (*GetTick)(void);
} LoRa_IO_t;

/* ========================================================================== */
/*                                 对象模型                                    */
/* ========================================================================== */

/**
 * @brief LoRa 设备对象
 */
typedef struct {
    // --- 接口 ---
    LoRa_IO_t io;
    
    // --- 回调 ---
    LoRa_RxHandler_t rx_callback;
    
    // --- 协议配置 (运行时可改) ---
    uint8_t head[2];
    uint8_t tail[2];
    
    // --- 私有属性 ---
    uint8_t         rx_buf[LORA_MAX_PACKET_SIZE];
    uint16_t        rx_idx;
    LoRa_RxState_e  rx_state;
    bool            is_initialized;
} LoRa_Dev_t;

/* ========================================================================== */
/*                                 核心 API                                    */
/* ========================================================================== */

// 1. 初始化与绑定
bool LoRa_Init(LoRa_Dev_t *dev, const LoRa_IO_t *io_interface);

// 2. 注册回调
void LoRa_RegisterCallback(LoRa_Dev_t *dev, LoRa_RxHandler_t cb);

// 3. 协议配置 (运行时修改包头包尾)
void LoRa_SetPacketHeader(LoRa_Dev_t *dev, uint8_t h0, uint8_t h1);
void LoRa_SetPacketTail(LoRa_Dev_t *dev, uint8_t t0, uint8_t t1);

// 4. 核心发送 (自动加头尾)
void LoRa_Send(LoRa_Dev_t *dev, const uint8_t *data, uint16_t len);

// 5. 核心处理 (需周期性调用)
void LoRa_Process(LoRa_Dev_t *dev);

/* ========================================================================== */
/*                                 配置 API                                    */
/* ========================================================================== */

void LoRa_EnterConfigMode(LoRa_Dev_t *dev);
void LoRa_EnterCommMode(LoRa_Dev_t *dev);
bool LoRa_SendAT(LoRa_Dev_t *dev, const char *cmd, const char *expect_resp, uint32_t timeout);

bool LoRa_SetChannel(LoRa_Dev_t *dev, uint8_t channel);
bool LoRa_SetPower(LoRa_Dev_t *dev, LoRa_Power_e power);
bool LoRa_SetAirRate(LoRa_Dev_t *dev, LoRa_Rate_e rate);

#endif // __MOD_LORA_H
