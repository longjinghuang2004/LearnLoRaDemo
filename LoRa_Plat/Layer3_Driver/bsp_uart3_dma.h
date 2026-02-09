/**
  ******************************************************************************
  * @file    bsp_uart3_dma.h
  * @author  xxx Project Team
  * @brief   Layer 3: USART3 底层驱动 (DMA接收 + 阻塞发送)
  *          用于连接 LoRa 模块，支持不定长数据包接收
  ******************************************************************************
  */

#ifndef __BSP_UART3_DMA_H
#define __BSP_UART3_DMA_H

#include "stm32f10x.h"
#include <stdio.h>

// --- 配置宏定义 ---
#define UART3_BAUDRATE      115200      // LoRa 模块配置模式通常固定 115200，通信模式可配
#define UART3_RX_BUF_SIZE   256         // 接收缓冲区大小 (根据最大 JSON 包长度设定)

// --- 全局变量声明 (供上层读取状态) ---
extern uint8_t  UART3_RxBuffer[UART3_RX_BUF_SIZE]; // 接收缓冲区
extern uint16_t UART3_RxLength;                    // 接收到的数据长度
extern uint8_t  UART3_RxFlag;                      // 接收完成标志 (1=完成, 0=未完成)

// --- 函数接口 ---

/**
 * @brief  初始化 USART3 + DMA + GPIO
 * @param  baudrate: 波特率
 */
void BSP_UART3_Init(uint32_t baudrate);

/**
 * @brief  发送字节数组 (阻塞式，确保数据发完)
 * @param  data: 数据指针
 * @param  len:  数据长度
 */
void BSP_UART3_SendBytes(uint8_t *data, uint16_t len);

/**
 * @brief  发送字符串
 * @param  str: 字符串指针
 */
void BSP_UART3_SendString(char *str);

/**
 * @brief  重置接收状态 (处理完数据后必须调用)
 *         重新开启 DMA 准备接收下一包
 */
void BSP_UART3_ResetRx(void);

// [新增] 彻底清空缓冲区内容，用于AT指令解析前
void BSP_UART3_ClearRxBuffer(void);

#endif // __BSP_UART3_DMA_H
