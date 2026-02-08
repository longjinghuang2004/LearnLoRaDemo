#include "bsp_uart3_dma.h"

// ... 变量定义保持不变 ...
uint8_t  UART3_RxBuffer[UART3_RX_BUF_SIZE];
uint16_t UART3_RxLength = 0;
uint8_t  UART3_RxFlag = 0;

// ... BSP_UART3_Init 函数保持不变 ...
// ... BSP_UART3_SendBytes 函数保持不变 ...
// ... BSP_UART3_SendString 函数保持不变 ...
// ... BSP_UART3_ResetRx 函数保持不变 ...

/**
  * @brief  USART3 中断服务函数
  *         [修复] 增加了对错误标志位的处理
  */
void USART3_IRQHandler(void)
{
    uint8_t clear_temp;
    
    // 1. 处理空闲中断 (接收到一帧数据)
    if (USART_GetITStatus(USART3, USART_IT_IDLE) != RESET)
    {
        // 清除 IDLE 标志位 (序列: 读SR -> 读DR)
        clear_temp = USART3->SR;
        clear_temp = USART3->DR;
        (void)clear_temp; 

        // 关闭 DMA
        DMA_Cmd(DMA1_Channel3, DISABLE);

        // 计算数据长度
        UART3_RxLength = UART3_RX_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);

        // 置位标志
        if (UART3_RxLength > 0)
        {
            UART3_RxFlag = 1;
        }
    }
    
    // 2. [重要修复] 处理错误标志 (ORE:溢出, NE:噪声, FE:帧错误, PE:校验)
    // 如果不清除这些错误，中断可能会卡死
    if (USART_GetFlagStatus(USART3, USART_FLAG_ORE) != RESET ||
        USART_GetFlagStatus(USART3, USART_FLAG_NE) != RESET ||
        USART_GetFlagStatus(USART3, USART_FLAG_FE) != RESET ||
        USART_GetFlagStatus(USART3, USART_FLAG_PE) != RESET)
    {
        // 读取 SR 和 DR 可以清除这些错误标志
        clear_temp = USART3->SR;
        clear_temp = USART3->DR;
        (void)clear_temp;
    }
}
