/**
  ******************************************************************************
  * @file    bsp_uart3_dma.c
  * @brief   Layer 3: USART3 驱动实现 (DMA Circular Mode)
  ******************************************************************************
  */

#include "bsp_uart3_dma.h"
#include <string.h>

// --- 内部变量 ---
// 接收缓冲区 (由 DMA 自动写入)
static uint8_t  s_RxBuffer[UART3_RX_BUF_SIZE]; 
// 软件读指针 (记录应用层读到了哪里)
static volatile uint16_t s_RxReadIndex = 0;

/**
  * @brief  初始化 USART3, GPIO, DMA (循环模式)
  */
void BSP_UART3_Init(uint32_t baudrate)
{
    // 1. 开启时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    // 2. GPIO 配置
    GPIO_InitTypeDef GPIO_InitStructure;
    // PB10 -> TX
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    // PB11 -> RX
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

    // 4. DMA 配置 (关键修改：DMA_Mode_Circular)
    DMA_DeInit(DMA1_Channel3);
    DMA_InitTypeDef DMA_InitStructure;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)s_RxBuffer;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = UART3_RX_BUF_SIZE;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    
    // [重点] 开启循环模式，DMA 永远不会停止，满了自动覆盖开头
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular; 
    
    DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);

    // 5. 开启 DMA 和 串口DMA接收请求
    DMA_Cmd(DMA1_Channel3, ENABLE);
    USART_DMACmd(USART3, USART_DMAReq_Rx, ENABLE);

    // 6. 中断配置 
    // 注意：循环模式下不需要 IDLE 中断来停止 DMA，
    // 但我们保留中断用于处理 ORE (溢出) 等错误，防止串口锁死。
    USART_ITConfig(USART3, USART_IT_RXNE, DISABLE); // 关闭接收中断，全靠DMA
    
    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; 
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 7. 使能串口
    USART_Cmd(USART3, ENABLE);
    
    // 初始化读指针
    s_RxReadIndex = 0;
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
  * @brief  [核心] 从循环缓冲区读取数据
  *         该函数会被主循环频繁调用
  */
uint16_t BSP_UART3_ReadRxBuffer(uint8_t *output_buf, uint16_t max_len)
{
    uint16_t bytes_read = 0;
    
    // 1. 计算 DMA 当前写到了哪里 (Head)
    // CNDTR 寄存器是递减的，所以：Head = 总大小 - 剩余大小
    uint16_t write_index = UART3_RX_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);
    
    // 2. 如果读指针 != 写指针，说明有新数据
    while (s_RxReadIndex != write_index && bytes_read < max_len)
    {
        // 读取一个字节
        output_buf[bytes_read++] = s_RxBuffer[s_RxReadIndex];
        
        // 移动读指针
        s_RxReadIndex++;
        
        // 处理回卷 (Wrap Around)
        if (s_RxReadIndex >= UART3_RX_BUF_SIZE)
        {
            s_RxReadIndex = 0;
        }
    }
    
    return bytes_read;
}

/**
  * @brief  清空缓冲区
  */
void BSP_UART3_ClearRxBuffer(void)
{
    // 只需要同步读指针到写指针的位置，相当于丢弃所有未读数据
    s_RxReadIndex = UART3_RX_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);
}

/**
  * @brief  USART3 中断服务函数
  *         仅用于处理错误标志，不再处理数据接收
  */
void USART3_IRQHandler(void)
{
    volatile uint8_t clear_temp;
    
    // 处理错误标志 (ORE:溢出, NE:噪声, FE:帧错误, PE:校验)
    if (USART_GetFlagStatus(USART3, USART_FLAG_ORE) != RESET ||
        USART_GetFlagStatus(USART3, USART_FLAG_NE) != RESET ||
        USART_GetFlagStatus(USART3, USART_FLAG_FE) != RESET ||
        USART_GetFlagStatus(USART3, USART_FLAG_PE) != RESET)
    {
        // 读取 SR 和 DR 清除错误，防止死机
        clear_temp = USART3->SR;
        clear_temp = USART3->DR;
        (void)clear_temp;
    }
}
