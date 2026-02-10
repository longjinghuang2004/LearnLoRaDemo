#include "lora_port.h"
#include "stm32f10x.h"
#include "Delay.h" // 假设 Delay.h 提供了 GetTick()
#include <string.h>

// --- 内部变量: DMA 缓冲区 ---
#define PORT_DMA_RX_BUF_SIZE 512
#define PORT_DMA_TX_BUF_SIZE 512

// 接收缓冲区 (DMA 循环写入)
static uint8_t  s_DmaRxBuf[PORT_DMA_RX_BUF_SIZE];
// 发送缓冲区 (DMA 读取)
static uint8_t  s_DmaTxBuf[PORT_DMA_TX_BUF_SIZE];

// 软件读指针 (追踪 DMA 接收进度)
static volatile uint16_t s_RxReadIndex = 0;
// 发送忙碌标志
static volatile bool s_TxDmaBusy = false;

void Port_Init(void)
{
    // 1. 时钟使能
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA, ENABLE);
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

    // PA4 -> MD0 (推挽输出)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // PA5 -> AUX (上拉输入)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 3. USART3 配置
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = LORA_UART_BAUDRATE;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStructure);

    // 4. DMA RX (Circular Mode) -> DMA1_Channel3
    DMA_DeInit(DMA1_Channel3);
    DMA_InitTypeDef DMA_InitStructure;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)s_DmaRxBuf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = PORT_DMA_RX_BUF_SIZE;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular;
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);

    // 5. DMA TX (Normal Mode) -> DMA1_Channel2
    DMA_DeInit(DMA1_Channel2);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)s_DmaTxBuf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = 0;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_Init(DMA1_Channel2, &DMA_InitStructure);
    
    DMA_ITConfig(DMA1_Channel2, DMA_IT_TC, ENABLE);

    // 6. 中断配置
    NVIC_InitTypeDef NVIC_InitStructure;
    
    // DMA TX 中断
    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 7. 启动
    DMA_Cmd(DMA1_Channel3, ENABLE); // 开启 RX DMA
    USART_DMACmd(USART3, USART_DMAReq_Rx | USART_DMAReq_Tx, ENABLE);
    USART_Cmd(USART3, ENABLE);
    
    // 默认通信模式
    Port_SetMD0(false);
}

void Port_SetMD0(bool level) {
    GPIO_WriteBit(GPIOA, GPIO_Pin_4, level ? Bit_SET : Bit_RESET);
}

bool Port_GetAUX(void) {
    return (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_5) == 1);
}

void Port_SetRST(bool level) {
    // 如果有 RST 引脚，在这里实现
    // GPIO_WriteBit(GPIOA, GPIO_Pin_X, level ? Bit_SET : Bit_RESET);
}

uint16_t Port_WriteData(const uint8_t *data, uint16_t len) {
    if (s_TxDmaBusy || len > PORT_DMA_TX_BUF_SIZE) return 0;
    
    s_TxDmaBusy = true;
    memcpy(s_DmaTxBuf, data, len);
    
    DMA_Cmd(DMA1_Channel2, DISABLE);
    DMA1_Channel2->CNDTR = len;
    DMA_Cmd(DMA1_Channel2, ENABLE);
    
    return len;
}

uint16_t Port_ReadData(uint8_t *buf, uint16_t max_len) {
    uint16_t cnt = 0;
    // 计算 DMA 当前写到了哪里 (Total - Remaining)
    uint16_t dma_write_idx = PORT_DMA_RX_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);
    
    while (s_RxReadIndex != dma_write_idx && cnt < max_len) {
        buf[cnt++] = s_DmaRxBuf[s_RxReadIndex++];
        if (s_RxReadIndex >= PORT_DMA_RX_BUF_SIZE) s_RxReadIndex = 0;
    }
    return cnt;
}

void Port_ClearRxBuffer(void) {
    s_RxReadIndex = PORT_DMA_RX_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);
}

uint32_t Port_GetTick(void) {
    return GetTick(); // 依赖 System/Delay.c
}

// DMA TX 中断服务函数
void DMA1_Channel2_IRQHandler(void) {
    if (DMA_GetITStatus(DMA1_IT_TC2)) {
        DMA_ClearITPendingBit(DMA1_IT_TC2);
        s_TxDmaBusy = false;
    }
}
