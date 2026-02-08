/**
  ******************************************************************************
  * @file    bsp_uart3_dma.c
  * @author  XYY Project Team
  * @brief   Layer 3: USART3 底层驱动实现
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

    // PB11 -> RX (浮空输入或上拉输入)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; // 推荐上拉，防止干扰
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
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DR;     // 外设地址: 串口数据寄存器
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)UART3_RxBuffer;      // 内存地址: 我们的缓冲区
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;                    // 方向: 外设 -> 内存
    DMA_InitStructure.DMA_BufferSize = UART3_RX_BUF_SIZE;                 // 缓冲区大小
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;      // 外设地址不增
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;               // 内存地址自增
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte; // 字节传输
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;                         // 普通模式 (非循环，方便处理不定长包)
    DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);

    // 5. 开启 DMA 和 串口DMA接收请求
    DMA_Cmd(DMA1_Channel3, ENABLE);
    USART_DMACmd(USART3, USART_DMAReq_Rx, ENABLE);

    // 6. 配置中断 (使用 IDLE 空闲中断来检测数据包结束)
    USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);

    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; // 优先级根据需要调整
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
        // 等待发送完成 (TXE: 发送数据寄存器空)
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
  *         处理 IDLE (空闲) 中断，意味着一帧数据接收完毕
  */
void USART3_IRQHandler(void)
{
    uint8_t clear_temp;
    
    // 检查是否是空闲中断
    if (USART_GetITStatus(USART3, USART_IT_IDLE) != RESET)
    {
        // 1. 清除 IDLE 标志位 (序列: 读SR -> 读DR)
        clear_temp = USART3->SR;
        clear_temp = USART3->DR;
        (void)clear_temp; // 防止编译器警告未使用变量

        // 2. 关闭 DMA 以防止数据在处理时被覆盖 (虽然在普通模式下DMA会自动停止，但手动关更安全)
        DMA_Cmd(DMA1_Channel3, DISABLE);

        // 3. 计算接收到的数据长度
        // 总大小 - 剩余传输量 = 已传输量
        UART3_RxLength = UART3_RX_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);

        // 4. 置位接收完成标志
        if (UART3_RxLength > 0)
        {
            UART3_RxFlag = 1;
        }
        
        // 注意：此时不要立即重启 DMA，等主循环处理完数据后调用 BSP_UART3_ResetRx 重启
    }
}
