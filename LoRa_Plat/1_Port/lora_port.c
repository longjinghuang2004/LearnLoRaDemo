#include "lora_port.h"
#include "stm32f10x.h"
#include <string.h>
#include "Delay.h" // 仅用于 GetTick

// --- DMA 缓冲区 ---
#define PORT_DMA_RX_BUF_SIZE 512
#define PORT_DMA_TX_BUF_SIZE 512

static uint8_t  s_DmaRxBuf[PORT_DMA_RX_BUF_SIZE];
static uint8_t  s_DmaTxBuf[PORT_DMA_TX_BUF_SIZE];
static volatile uint16_t s_RxReadIndex = 0;

// --- 状态标志 ---
static volatile bool s_TxDmaBusy = false;
static volatile bool s_IsAuxBusy = false; // 由 EXTI 维护

void Port_Init(void)
{
    // 1. 时钟使能
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA | RCC_APB2Periph_AFIO, ENABLE);
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

    // PA4 -> MD0
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // PA5 -> AUX (输入)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 3. USART3 配置
    Port_ReInitUart(LORA_UART_BAUDRATE);

    // 4. DMA RX (Circular) -> DMA1_Channel3
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

    // 5. DMA TX (Normal) -> DMA1_Channel2
    DMA_DeInit(DMA1_Channel2);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART3->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)s_DmaTxBuf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = 0;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_Init(DMA1_Channel2, &DMA_InitStructure);
    DMA_ITConfig(DMA1_Channel2, DMA_IT_TC, ENABLE);

    // 6. NVIC 配置
    NVIC_InitTypeDef NVIC_InitStructure;
    
    // DMA TX 中断
    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel2_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 7. EXTI 配置 (PA5 -> AUX)
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource5);
    
    EXTI_InitTypeDef EXTI_InitStructure;
    EXTI_InitStructure.EXTI_Line = EXTI_Line5;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling; // 双边沿触发
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    // EXTI 中断优先级 (低优先级，防止打断通信)
    NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 3;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 8. 启动
    DMA_Cmd(DMA1_Channel3, ENABLE);
    USART_DMACmd(USART3, USART_DMAReq_Rx | USART_DMAReq_Tx, ENABLE);
    USART_Cmd(USART3, ENABLE);
    
    // 初始状态同步
    Port_SetMD0(false);
    s_IsAuxBusy = (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_5) == 1);
}

void Port_SetMD0(bool level) {
    GPIO_WriteBit(GPIOA, GPIO_Pin_4, level ? Bit_SET : Bit_RESET);
}

bool Port_GetAUX_Raw(void) {
    return (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_5) == 1);
}

bool Port_IsAuxBusy(void) {
    return s_IsAuxBusy;
}

void Port_SetRST(bool level) {
    // 如有 RST 引脚，在此实现
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
    return GetTick(); 
}

void Port_ReInitUart(uint32_t baudrate) {
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStructure);
    USART_Cmd(USART3, ENABLE);
}

// --- 中断服务函数 ---

// DMA TX 完成
void DMA1_Channel2_IRQHandler(void) {
    if (DMA_GetITStatus(DMA1_IT_TC2)) {
        DMA_ClearITPendingBit(DMA1_IT_TC2);
        s_TxDmaBusy = false;
    }
}

// AUX 变化 (EXTI Line 5)
void EXTI9_5_IRQHandler(void) {
    if (EXTI_GetITStatus(EXTI_Line5) != RESET) {
        // 读取当前电平更新标志
        // 简单的防抖可以在这里做，但为了响应速度，直接读
        s_IsAuxBusy = (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_5) == 1);
        EXTI_ClearITPendingBit(EXTI_Line5);
    }
}

// 随机数种子 (ADC悬空)
uint32_t Port_GetRandomSeed(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_ADCCLKConfig(RCC_PCLK2_Div6); 
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    ADC_InitTypeDef ADC_InitStructure;
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);
    ADC_Cmd(ADC1, ENABLE);
    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));
    uint32_t seed = 0;
    for(int i=0; i<32; i++) {
        ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_1Cycles5);
        ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));
        if (ADC_GetConversionValue(ADC1) & 0x01) seed |= (1 << i);
    }
    ADC_Cmd(ADC1, DISABLE);
    if (seed == 0) seed = GetTick() ^ 0x5A5A5A5A;
    return seed;
}
