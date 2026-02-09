/**
  ******************************************************************************
  * @file    bsp_lora_uart.c
  * @author  None
  * @brief   Layer 3: LoRa UART 硬件驱动实现
  ******************************************************************************
  */

#include "bsp_lora_uart.h"
#include <string.h>

/* ========================================================================== */
/*                                 内部变量                                   */
/* ========================================================================== */

// --- 接收相关 ---
// DMA 循环接收缓冲区 (硬件自动写入)
static uint8_t  s_RxBuffer[LORA_DRIVER_RX_BUF_SIZE]; 
// 软件读指针 (记录应用层读到了哪里)
static volatile uint16_t s_RxReadIndex = 0;

// --- 发送相关 ---
// DMA 发送缓冲区 (软件写入，DMA读取)
static uint8_t  s_TxBuffer[LORA_DRIVER_TX_BUF_SIZE];
// 发送忙碌标志位
static volatile bool s_TxBusy = false;


/* ========================================================================== */
/*                                 函数实现                                   */
/* ========================================================================== */

/**
  * @brief  初始化 LoRa UART 硬件
  * @note   配置为: USART3, PB10(TX), PB11(RX), DMA1_Ch2(TX), DMA1_Ch3(RX)
  */
void BSP_LoRa_UART_Init(void)
{
    // 1. 开启时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    // 2. GPIO 配置
    GPIO_InitTypeDef GPIO_InitStructure;
    // PB10 -> TX (复用推挽)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    // PB11 -> RX (上拉输入)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // 3. USART3 参数配置
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = LORA_UART_BAUDRATE;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStructure);

    // 4. DMA RX 配置 (关键: DMA_Mode_Circular)
    // USART3_RX 对应 DMA1_Channel3
    DMA_DeInit(DMA1_Channel3);
    DMA_InitTypeDef DMA_InitStructure;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)s_RxBuffer;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC; // 外设 -> 内存
    DMA_InitStructure.DMA_BufferSize = LORA_DRIVER_RX_BUF_SIZE;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular; // 循环模式
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);

    // 5. DMA TX 配置 (关键: DMA_Mode_Normal)
    // USART3_TX 对应 DMA1_Channel2
    DMA_DeInit(DMA1_Channel2);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)s_TxBuffer;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST; // 内存 -> 外设
    DMA_InitStructure.DMA_BufferSize = 0; // 初始长度为0
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal; // 普通模式 (发完一次停)
    DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel2, &DMA_InitStructure);
    
    // 开启 TX 完成中断 (用于清除忙碌标志)
    DMA_ITConfig(DMA1_Channel2, DMA_IT_TC, ENABLE);

    // 6. 开启 DMA 和 串口DMA请求
    DMA_Cmd(DMA1_Channel3, ENABLE); // 立即开启 RX DMA
    USART_DMACmd(USART3, USART_DMAReq_Rx | USART_DMAReq_Tx, ENABLE);

    // 7. NVIC 配置
    NVIC_InitTypeDef NVIC_InitStructure;
    
    // DMA1_Channel2 (TX) 中断
    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; 
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // USART3 全局中断 (仅用于处理错误，如ORE溢出)
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1; 
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 8. 使能串口
    USART_Cmd(USART3, ENABLE);
    
    // 初始化状态
    s_RxReadIndex = 0;
    s_TxBusy = false;
}

/**
  * @brief  [非阻塞] 尝试启动 DMA 发送
  */
uint16_t BSP_LoRa_UART_Send(const uint8_t *data, uint16_t len)
{
    // 1. 参数检查
    if (len == 0 || len > LORA_DRIVER_TX_BUF_SIZE) return 0;
    
    // 2. 状态检查 (如果 DMA 正在搬运上一包，则拒绝本次发送)
    //    注意：Layer 2 应该有队列机制来处理这种情况
    if (s_TxBusy) return 0;

    // 3. 锁定状态
    s_TxBusy = true;

    // 4. 填充数据到 DMA 专用缓冲区
    //    (必须拷贝，因为上层 data 指针指向的内存可能会在发送完成前被修改)
    memcpy(s_TxBuffer, data, len);

    // 5. 配置并启动 DMA
    DMA_Cmd(DMA1_Channel2, DISABLE);
    DMA1_Channel2->CNDTR = len;
    DMA_Cmd(DMA1_Channel2, ENABLE);

    return len;
}

/**
  * @brief  检查发送是否忙碌
  */
bool BSP_LoRa_UART_IsTxBusy(void)
{
    return s_TxBusy;
}

/**
  * @brief  从 DMA 循环缓冲区读取数据
  */
uint16_t BSP_LoRa_UART_Read(uint8_t *output_buf, uint16_t max_len)
{
    uint16_t bytes_read = 0;
    
    // 1. 计算 DMA 当前写到了哪里 (Head)
    //    CNDTR 是递减计数器，所以 Head = Size - CNDTR
    uint16_t dma_write_index = LORA_DRIVER_RX_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);
    
    // 2. 循环读取直到追上 DMA 的写指针
    while (s_RxReadIndex != dma_write_index && bytes_read < max_len)
    {
        output_buf[bytes_read++] = s_RxBuffer[s_RxReadIndex];
        
        s_RxReadIndex++;
        // 处理回卷
        if (s_RxReadIndex >= LORA_DRIVER_RX_BUF_SIZE)
        {
            s_RxReadIndex = 0;
        }
    }
    
    return bytes_read;
}

/**
  * @brief  清空接收缓冲区
  */
void BSP_LoRa_UART_ClearRx(void)
{
    // 将读指针直接同步到当前的写指针位置，相当于丢弃所有未读数据
    s_RxReadIndex = LORA_DRIVER_RX_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);
}

/* ========================================================================== */
/*                                 中断服务函数                                */
/* ========================================================================== */

/**
  * @brief  DMA1 Channel2 (USART3_TX) 中断
  *         发送完成后触发，用于清除 Busy 标志
  */
void DMA1_Channel2_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA1_IT_TC2))
    {
        DMA_ClearITPendingBit(DMA1_IT_TC2);
        // 发送完成，释放忙碌状态
        s_TxBusy = false;
    }
}

/**
  * @brief  USART3 全局中断
  *         仅处理硬件错误，防止串口锁死
  */
void USART3_IRQHandler(void)
{
    volatile uint8_t clear_temp;
    
    // 处理 ORE (Overrun Error) 等错误
    if (USART_GetFlagStatus(USART3, USART_FLAG_ORE) != RESET)
    {
        // 读取 SR 和 DR 清除错误
        clear_temp = USART3->SR;
        clear_temp = USART3->DR;
        (void)clear_temp;
    }
}
