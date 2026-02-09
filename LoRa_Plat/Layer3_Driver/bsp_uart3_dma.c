/**
  ******************************************************************************
  * @file    bsp_uart3_dma.c
  * @author  XYY Project Team
  * @brief   Layer 3: USART3 底层驱动实现 (修复中断死机问题)
  ******************************************************************************
  */

#include "bsp_uart3_dma.h"

// --- 变量定义 ---
uint8_t  UART3_RxBuffer[UART3_RX_BUF_SIZE]; // DMA 接收目标缓冲区
uint16_t UART3_RxLength = 0;                // 本次接收到的字节数
uint8_t  UART3_RxFlag = 0;                  // 接收完成标志

/**
  * @brief  初始化 USART3, GPIO, DMA 和 NVIC
  */
void BSP_UART3_Init(uint32_t baudrate)
{
    // 1. 开启时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);  // GPIOB 时钟
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE); // USART3 时钟
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);     // DMA1 时钟

    // 2. GPIO 配置
    GPIO_InitTypeDef GPIO_InitStructure;

    // PB10 -> TX (复用推挽输出)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // PB11 -> RX (上拉输入，防止悬空干扰)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 3. USART3 参数配置
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStructure);

    // 4. DMA 配置 (DMA1_Channel3 对应 USART3_RX)
    DMA_DeInit(DMA1_Channel3);
    DMA_InitTypeDef DMA_InitStructure;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DR;     // 外设地址
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)UART3_RxBuffer;      // 内存地址
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;                    // 外设 -> 内存
    DMA_InitStructure.DMA_BufferSize = UART3_RX_BUF_SIZE;                 // 大小
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;      // 外设不增
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;               // 内存自增
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; 
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;                         // 普通模式
    DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);

    // 5. 开启 DMA 和 串口DMA接收请求
    DMA_Cmd(DMA1_Channel3, ENABLE);
    USART_DMACmd(USART3, USART_DMAReq_Rx, ENABLE);

    // 6. 配置中断 (使用 IDLE 空闲中断)
    USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);

    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; 
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 7. 使能串口
    USART_Cmd(USART3, ENABLE);
}

/**
  * @brief  发送字节数组
  */
void BSP_UART3_SendBytes(uint8_t *data, uint16_t len)
{
    for (uint16_t i = 0; i < len; i++)
    {
        USART_SendData(USART3, data[i]);
        while (USART_GetFlagStatus(USART3, USART_FLAG_TXE) == RESET);
    }
}

/**
  * @brief  发送字符串
  */
void BSP_UART3_SendString(char *str)
{
    while (*str)
    {
        BSP_UART3_SendBytes((uint8_t *)str, 1);
        str++;
    }
}

/**
  * @brief  重置接收状态，准备下一次接收
  */
void BSP_UART3_ResetRx(void)
{
    // 1. 关闭 DMA
    DMA_Cmd(DMA1_Channel3, DISABLE);
    
    // 2. 清除标志位
    UART3_RxLength = 0;
    UART3_RxFlag = 0;
    
    // 3. 重置 DMA 传输数量
    DMA_SetCurrDataCounter(DMA1_Channel3, UART3_RX_BUF_SIZE);
    
    // 4. 重新开启 DMA
    DMA_Cmd(DMA1_Channel3, ENABLE);
}

/**
  * @brief  USART3 中断服务函数
  *         [重要修复] 增加了错误标志清除，防止死机
  */
void USART3_IRQHandler(void)
{
    volatile uint8_t clear_temp; // 使用 volatile 防止编译器优化掉读取操作
    
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
    
    // 2. [关键修复] 处理错误标志 (ORE:溢出, NE:噪声, FE:帧错误, PE:校验)
    // 如果发生波特率不匹配，FE和NE标志会置位。如果不清除，中断会锁死。
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


void BSP_UART3_ClearRxBuffer(void)
{
    DMA_Cmd(DMA1_Channel3, DISABLE);
    memset(UART3_RxBuffer, 0, UART3_RX_BUF_SIZE);
    UART3_RxLength = 0;
    UART3_RxFlag = 0;
    DMA_SetCurrDataCounter(DMA1_Channel3, UART3_RX_BUF_SIZE);
    DMA_Cmd(DMA1_Channel3, ENABLE);
}