/**
  ******************************************************************************
  * @file    mod_lora.h
  * @author  None
  * @brief   Layer 2: LoRa 中间件核心头文件 (平台无关)
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

// 中间件内部接收缓冲区大小 (用于拼包)
#define LORA_INTERNAL_RX_BUF_SIZE   256

// 默认包头包尾 (ASCII: CM, \n\n)
#define LORA_DEFAULT_HEAD_0         'C'
#define LORA_DEFAULT_HEAD_1         'M'
#define LORA_DEFAULT_TAIL_0         '\n'
#define LORA_DEFAULT_TAIL_1         '\n'

/* ========================================================================== */
/*                                 数据类型定义                                */
/* ========================================================================== */

/**
 * @brief 底层硬件接口结构体 (移植时需填充此结构体)
 */
typedef struct {
    // 发送函数 (非阻塞，返回实际发送长度)
    uint16_t (*Send)(const uint8_t *data, uint16_t len);
    
    // 接收函数 (非阻塞，从底层DMA缓冲区读取数据)
    uint16_t (*Read)(uint8_t *buf, uint16_t max_len);
    
    // GPIO控制: 设置 MD0 引脚电平 (0:通信, 1:配置)
    void (*SetMD0)(uint8_t level);
    
    // GPIO读取: 读取 AUX 引脚电平 (1:忙, 0:闲)
    uint8_t (*ReadAUX)(void);
    
    // 延时函数 (毫秒)
    void (*DelayMs)(uint32_t ms);
    
    // 获取系统时间戳 (毫秒)
    uint32_t (*GetTick)(void);
} LoRa_Driver_t;

/**
 * @brief LoRa 设备对象结构体
 */
typedef struct {
    // --- 私有成员 (用户勿动) ---
    LoRa_Driver_t drv;              // 底层驱动接口
    uint8_t  rx_buf[LORA_INTERNAL_RX_BUF_SIZE]; // 内部接收缓冲区
    uint16_t rx_index;              // 当前接收索引
    bool     is_receiving_packet;   // 是否正在接收一个包 (已收到包头)
    
    // --- 配置成员 ---
    uint8_t  head[2];               // 包头 (2字节)
    uint8_t  tail[2];               // 包尾 (2字节)
} LoRa_Dev_t;

/* ========================================================================== */
/*                                 函数接口声明                                */
/* ========================================================================== */

/**
  * @brief  1. 初始化 LoRa 中间件
  * @param  dev: LoRa 设备对象指针
  * @param  driver: 填充好的底层驱动接口结构体
  * @retval true: 初始化成功
  */
bool LoRa_Init(LoRa_Dev_t *dev, const LoRa_Driver_t *driver);

/**
  * @brief  2. 发送 AT 指令并等待响应 (阻塞式)
  * @param  dev: LoRa 设备对象指针
  * @param  cmd: AT 指令字符串 (如 "AT+ADDR?")
  * @param  resp_buf: 用于存储响应内容的缓冲区 (可以为 NULL)
  * @param  resp_max_len: 响应缓冲区最大长度
  * @param  expect_str: 期望收到的响应子串 (如 "OK")，如果为 NULL 则不检查
  * @param  timeout_ms: 超时时间 (ms)
  * @retval true: 发送成功且收到了期望的响应
  */
bool LoRa_SendAT(LoRa_Dev_t *dev, const char *cmd, char *resp_buf, uint16_t resp_max_len, const char *expect_str, uint32_t timeout_ms);

/**
  * @brief  3. 单字节发送 (透传，不加头尾)
  */
void LoRa_SendByteRaw(LoRa_Dev_t *dev, uint8_t byte);

/**
  * @brief  4. 字符串/数据发送 (透传，不加头尾)
  */
void LoRa_SendDataRaw(LoRa_Dev_t *dev, const uint8_t *data, uint16_t len);

/**
  * @brief  4.1 发送数据包 (自动添加包头包尾)
  * @note   最终发送内容: [Head0][Head1][Data...][Tail0][Tail1]
  */
void LoRa_SendPacket(LoRa_Dev_t *dev, const uint8_t *data, uint16_t len);

/**
  * @brief  5. 接收数据包 (轮询调用)
  * @note   需在主循环中频繁调用。
  *         该函数会自动从底层拉取数据，并进行包头包尾匹配。
  * @param  dev: LoRa 设备对象指针
  * @param  out_buf: 用户接收缓冲区，用于存放解析出的 Payload (不含头尾)
  * @param  max_len: 用户缓冲区最大长度
  * @retval uint16_t: 返回实际收到的 Payload 长度。如果为 0 表示未收到完整包。
  */
uint16_t LoRa_ReceivePacket(LoRa_Dev_t *dev, uint8_t *out_buf, uint16_t max_len);

/**
  * @brief  6. 设置包头 (2字节)
  */
void LoRa_SetPacketHeader(LoRa_Dev_t *dev, uint8_t h0, uint8_t h1);

/**
  * @brief  7. 设置包尾 (2字节)
  */
void LoRa_SetPacketTail(LoRa_Dev_t *dev, uint8_t t0, uint8_t t1);

/**
  * @brief  辅助: 驱动 LoRa 状态机 (需在主循环调用)
  * @note   主要用于处理 AUX 状态检测等后台任务 (目前版本暂为空，预留接口)
  */
void LoRa_Process(LoRa_Dev_t *dev);

#endif // __MOD_LORA_H
