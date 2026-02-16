/**
  ******************************************************************************
  * @file    bsp_lora_uart.h
  * @author  None
  * @brief   Layer 3: LoRa 专用 UART 硬件驱动层 (HAL)
  *          @note
  *          - 接收: DMA 循环模式 (Circular Mode)，解决高速丢包。
  *          - 发送: DMA 普通模式 (Normal Mode)，支持非阻塞发送。
  *          - 依赖: STM32F10x Standard Peripheral Library
  ******************************************************************************
  */

#ifndef __BSP_LORA_UART_H
#define __BSP_LORA_UART_H

#include "stm32f10x.h"
#include <stdint.h>
#include <stdbool.h>

/* ========================================================================== */
/*                                 宏定义配置                                  */
/* ========================================================================== */

/** 
 * @brief LoRa 模块通信波特率 
 */
#define LORA_UART_BAUDRATE      115200

/** 
 * @brief 底层接收缓冲区大小 (必须是2的幂次，如 256, 512) 
 * @note  这是 DMA 自动写入的原始缓冲区
 */
#define LORA_DRIVER_RX_BUF_SIZE 512

/** 
 * @brief 底层发送缓冲区大小
 * @note  用于 DMA 发送的临时暂存
 */
#define LORA_DRIVER_TX_BUF_SIZE 512


/* ========================================================================== */
/*                                函数接口声明                                 */
/* ========================================================================== */

/**
  * @brief  初始化 LoRa 相关的 UART 硬件 (GPIO, USART, DMA, NVIC)
  * @param  None
  * @retval None
  */
void BSP_LoRa_UART_Init(void);

/**
  * @brief  [非阻塞] 尝试启动 DMA 发送数据
  * @note   如果 DMA 正在忙（上一包未发完），此函数会直接返回 0 (失败)。
  *         上层 (Layer 2) 负责处理重试或队列缓冲。
  * @param  data 指向要发送的数据缓冲区
  * @param  len  要发送的数据长度
  * @retval uint16_t 成功放入发送缓冲区的字节数 (成功返回 len, 失败返回 0)
  */
uint16_t BSP_LoRa_UART_Send(const uint8_t *data, uint16_t len);

/**
  * @brief  检查底层发送是否忙碌
  * @param  None
  * @retval true: DMA正在发送中; false: 空闲
  */
bool BSP_LoRa_UART_IsTxBusy(void);

/**
  * @brief  [非阻塞] 从 DMA 循环缓冲区读取新收到的数据
  * @note   此函数应在主循环中被频繁调用
  * @param  output_buf 用户提供的接收缓冲区
  * @param  max_len    output_buf 的最大容量
  * @retval uint16_t   实际读取到的字节数
  */
uint16_t BSP_LoRa_UART_Read(uint8_t *output_buf, uint16_t max_len);

/**
  * @brief  清空底层接收缓冲区 (重置读写指针)
  * @param  None
  * @retval None
  */
void BSP_LoRa_UART_ClearRx(void);

#endif // __BSP_LORA_UART_H
