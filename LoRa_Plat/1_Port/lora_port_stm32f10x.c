/**
  ******************************************************************************
  * @file    lora_port_stm32f10x.c
  * @author  LoRaPlat Team
  * @brief   STM32F103 硬件接口实现 V3.3.0
  ******************************************************************************
  */

#include "lora_port.h"
#include "lora_osal.h"
#include "stm32f10x.h"
#include <string.h>

// --- DMA 缓冲区配置 ---
#define PORT_DMA_RX_BUF_SIZE 512
#define PORT_DMA_TX_BUF_SIZE 512

// 放在静态区，避免栈溢出
static uint8_t  s_DmaRxBuf[PORT_DMA_RX_BUF_SIZE];
static uint8_t  s_DmaTxBuf[PORT_DMA_TX_BUF_SIZE];
static volatile uint16_t s_RxReadIndex = 0;

// --- 状态标志 ---
static volatile bool s_TxDmaBusy = false;

// ============================================================
//                    1. 初始化与配置
// ============================================================

void LoRa_Port_Init(uint32_t baudrate)
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
    LoRa_Port_ReInitUart(baudrate); 

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

    // 7. 启动
    DMA_Cmd(DMA1_Channel3, ENABLE);
    USART_DMACmd(USART3, USART_DMAReq_Rx | USART_DMAReq_Tx, ENABLE);
    USART_Cmd(USART3, ENABLE);
    
    // 初始状态同步
    LoRa_Port_SetMD0(false);
    LoRa_Port_SyncAuxState(); 
}

void LoRa_Port_ReInitUart(uint32_t baudrate) {
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStructure);
    
    USART_DMACmd(USART3, USART_DMAReq_Rx | USART_DMAReq_Tx, ENABLE);
    USART_Cmd(USART3, ENABLE);
}

// ============================================================
//                    2. 引脚控制
// ============================================================

void LoRa_Port_SetMD0(bool level) {
    GPIO_WriteBit(GPIOA, GPIO_Pin_4, level ? Bit_SET : Bit_RESET);
}

void LoRa_Port_SetRST(bool level) {
    // 如有 RST 引脚，在此实现
    // GPIO_WriteBit(GPIOA, GPIO_Pin_X, level ? Bit_SET : Bit_RESET);
    (void)level;
}

bool LoRa_Port_GetAUX(void) {
    return (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_5) == 1);
}

void LoRa_Port_SyncAuxState(void) {
    OSAL_EnterCritical();
    // 强制复位 DMA 硬件状态
    DMA_Cmd(DMA1_Channel2, DISABLE);
    DMA1_Channel2->CNDTR = 0;
    s_TxDmaBusy = false;
    OSAL_ExitCritical();
}

// ============================================================
//                    3. 发送接口 (TX)
// ============================================================

bool LoRa_Port_IsTxBusy(void) {
    return s_TxDmaBusy;
}

uint16_t LoRa_Port_TransmitData(const uint8_t *data, uint16_t len) {
    if (len == 0 || len > PORT_DMA_TX_BUF_SIZE) return 0;

    OSAL_EnterCritical();

    // [关键修复] 双重检查：软件标志 + 硬件计数器
    // 防止上层在 IsTxBusy 返回 false 后，硬件尚未完全就绪的微小间隙
    if (s_TxDmaBusy || DMA_GetCurrDataCounter(DMA1_Channel2) != 0) {
        OSAL_ExitCritical();
        return 0; // 忙，拒绝发送
    }

    // 1. 标记忙碌
    s_TxDmaBusy = true;
    
    // 2. 填充数据 (安全，因为已确认 DMA 不忙)
    memcpy(s_DmaTxBuf, data, len);
    
    // 3. 启动 DMA
    DMA_Cmd(DMA1_Channel2, DISABLE);
    DMA1_Channel2->CNDTR = len;
    DMA_Cmd(DMA1_Channel2, ENABLE);
    
    OSAL_ExitCritical();
    
    return len;
}

// ============================================================
//                    4. 接收接口 (RX)
// ============================================================

uint16_t LoRa_Port_ReceiveData(uint8_t *buf, uint16_t max_len) {
    uint16_t cnt = 0;
    // 获取 DMA 当前写入位置 (硬件指针)
    uint16_t dma_write_idx = PORT_DMA_RX_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);
    
    // 循环读取直到追上硬件指针
    while (s_RxReadIndex != dma_write_idx && cnt < max_len) {
        buf[cnt++] = s_DmaRxBuf[s_RxReadIndex++];
        if (s_RxReadIndex >= PORT_DMA_RX_BUF_SIZE) s_RxReadIndex = 0;
    }
    return cnt;
}

void LoRa_Port_ClearRxBuffer(void) {
    s_RxReadIndex = PORT_DMA_RX_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);
}

// ============================================================
//                    5. 其他能力
// ============================================================

uint32_t LoRa_Port_GetEntropy32(void) {
    // 简单的 ADC 悬空采样 (保持原逻辑)
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
    
    if (seed == 0) seed = OSAL_GetTick() ^ 0x5A5A5A5A;
    return seed;
}

// ============================================================
//                    6. 中断服务函数
// ============================================================

// DMA TX 完成
void DMA1_Channel2_IRQHandler(void) {
    if (DMA_GetITStatus(DMA1_IT_TC2)) {
        DMA_ClearITPendingBit(DMA1_IT_TC2);
        s_TxDmaBusy = false;
    }
}
