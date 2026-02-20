/**
  ******************************************************************************
  * @file    lora_port_stm32f10x.c
  * @author  LoRaPlat Team
  * @brief   STM32F103 硬件接口实现 V3.4.0
  ******************************************************************************
  */

#include "lora_port.h"
#include "lora_osal.h"
#include "stm32f10x.h"
#include <string.h>

// --- DMA 缓冲区配置 ---
#define PORT_DMA_RX_BUF_SIZE 512
#define PORT_DMA_TX_BUF_SIZE 512

static uint8_t  s_DmaRxBuf[PORT_DMA_RX_BUF_SIZE];
static uint8_t  s_DmaTxBuf[PORT_DMA_TX_BUF_SIZE];
static volatile uint16_t s_RxReadIndex = 0;

// --- 状态标志 ---
static volatile bool s_TxDmaBusy = false;

// [新增] 硬件事件挂起标志 (用于低功耗唤醒判断)
static volatile bool s_HwEventPending = false;

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

    // [新增] 配置 AUX (PA5) 外部中断
    GPIO_EXTILineConfig(GPIO_PortSourceGPIOA, GPIO_PinSource5);

    EXTI_InitTypeDef EXTI_InitStructure;
    EXTI_InitStructure.EXTI_Line = EXTI_Line5;
    EXTI_InitStructure.EXTI_Mode = EXTI_Mode_Interrupt;
    EXTI_InitStructure.EXTI_Trigger = EXTI_Trigger_Rising_Falling; // 双边沿触发(忙/闲变化都唤醒)
    EXTI_InitStructure.EXTI_LineCmd = ENABLE;
    EXTI_Init(&EXTI_InitStructure);

    // 3. USART3 配置
    LoRa_Port_ReInitUart(baudrate); 
    
    // [新增] 开启 IDLE 中断 (用于接收唤醒)
    USART_ITConfig(USART3, USART_IT_IDLE, ENABLE);

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

    // [新增] AUX EXTI 中断
    NVIC_InitStructure.NVIC_IRQChannel = EXTI9_5_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2; // 优先级稍低
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_Init(&NVIC_InitStructure);

    // [新增] USART3 全局中断 (处理 IDLE)
    NVIC_InitStructure.NVIC_IRQChannel = USART3_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
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
    (void)level;
}

bool LoRa_Port_GetAUX(void) {
    return (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_5) == 1);
}

void LoRa_Port_SyncAuxState(void) {
    uint32_t primask = OSAL_EnterCritical();
    DMA_Cmd(DMA1_Channel2, DISABLE);
    DMA1_Channel2->CNDTR = 0;
    s_TxDmaBusy = false;
    OSAL_ExitCritical(primask);
}

// ============================================================
//                    3. 发送接口 (TX)
// ============================================================

bool LoRa_Port_IsTxBusy(void) {
    return s_TxDmaBusy;
}

uint16_t LoRa_Port_TransmitData(const uint8_t *data, uint16_t len) {
    if (len == 0 || len > PORT_DMA_TX_BUF_SIZE) return 0;
    
    uint32_t primask = OSAL_EnterCritical();

    if (s_TxDmaBusy || DMA_GetCurrDataCounter(DMA1_Channel2) != 0) {
        OSAL_ExitCritical(primask);
        return 0; 
    }

    s_TxDmaBusy = true;
    memcpy(s_DmaTxBuf, data, len);
    
    DMA_Cmd(DMA1_Channel2, DISABLE);
    DMA1_Channel2->CNDTR = len;
    DMA_Cmd(DMA1_Channel2, ENABLE);
    
    OSAL_ExitCritical(primask);
    return len;
}

// ============================================================
//                    4. 接收接口 (RX)
// ============================================================

uint16_t LoRa_Port_ReceiveData(uint8_t *buf, uint16_t max_len) {
    if (!buf || max_len == 0) return 0;
    
    uint16_t cnt = 0;
    uint16_t dma_write_idx = PORT_DMA_RX_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);
    
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
    // ... (保持原有的 ADC 随机数逻辑不变) ...
    return OSAL_GetTick() ^ 0x12345678; // 简化示例
}

// ============================================================
//                    6. 低功耗支持 (新增实现)
// ============================================================

void LoRa_Port_NotifyHwEvent(void) {
    s_HwEventPending = true;
}

bool LoRa_Port_CheckAndClearHwEvent(void) {
    uint32_t primask = OSAL_EnterCritical();
    bool ret = s_HwEventPending;
    s_HwEventPending = false; // 读后即焚
    OSAL_ExitCritical(primask);
    return ret;
}

// ============================================================
//                    7. 中断服务函数
// ============================================================

// DMA TX 完成
void DMA1_Channel2_IRQHandler(void) {
    if (DMA_GetITStatus(DMA1_IT_TC2)) {
        DMA_ClearITPendingBit(DMA1_IT_TC2);
        s_TxDmaBusy = false;
        // TX 完成也是一种硬件事件，可能需要触发 FSM 状态流转
        LoRa_Port_NotifyHwEvent(); 
    }
}
