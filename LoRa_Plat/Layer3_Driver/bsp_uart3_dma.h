/**
  ******************************************************************************
  * @file    bsp_uart3_dma.h
  * @brief   Layer 3: USART3 驱动 (DMA 循环接收模式)
  *          彻底解决 115200 波特率下的丢包问题
  ******************************************************************************
  */

#ifndef __BSP_UART3_DMA_H
#define __BSP_UART3_DMA_H

#include "stm32f10x.h"
#include <stdio.h>

// --- 配置宏定义 ---
#define UART3_BAUDRATE      115200      
// 增加缓冲区大小以应对高速流，必须是 2 的幂次方便回卷（可选，这里设为512）
#define UART3_RX_BUF_SIZE   512         

// --- 函数接口 ---

/**
 * @brief  初始化 USART3 + DMA (循环模式)
 * @param  baudrate: 波特率
 */
void BSP_UART3_Init(uint32_t baudrate);

/**
 * @brief  发送字节数组 (阻塞式)
 */
void BSP_UART3_SendBytes(uint8_t *data, uint16_t len);

/**
 * @brief  发送字符串
 */
void BSP_UART3_SendString(char *str);

/**
 * @brief  [核心函数] 从 DMA 循环缓冲区读取新数据
 * @param  output_buf: 用户提供的接收缓冲区
 * @param  max_len:    用户缓冲区最大容量
 * @return uint16_t:   实际读取到的字节数
 */
uint16_t BSP_UART3_ReadRxBuffer(uint8_t *output_buf, uint16_t max_len);

/**
 * @brief  清空接收缓冲区（重置读写指针）
 */
void BSP_UART3_ClearRxBuffer(void);

#endif // __BSP_UART3_DMA_H
