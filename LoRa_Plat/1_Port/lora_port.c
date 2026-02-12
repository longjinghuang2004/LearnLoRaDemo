#include "lora_port.h"
#include "stm32f10x.h"
#include "Delay.h" 
#include <string.h>

// ============================================================
//                    1. 内部配置与变量
// ============================================================

#define LORA_UART           USART3
#define LORA_UART_BAUD      115200
#define PORT_DMA_RX_SIZE    512
#define PORT_DMA_TX_SIZE    512

static uint8_t  s_DmaRxBuf[PORT_DMA_RX_SIZE];
static uint8_t  s_DmaTxBuf[PORT_DMA_TX_SIZE];
static volatile uint16_t s_RxReadIndex = 0;

// ============================================================
//                    2. 私有函数实现
// ============================================================

static void _STM32_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOB | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_USART3, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    
    // PB10 TX
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &GPIO_InitStructure);
    
    // PB11 RX
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_11;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU; 
    GPIO_Init(GPIOB, &GPIO_InitStructure);

    // PA4 MD0
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // PA5 AUX
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = LORA_UART_BAUD;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(LORA_UART, &USART_InitStructure);

    // DMA RX
    DMA_DeInit(DMA1_Channel3);
    DMA_InitTypeDef DMA_InitStructure;
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&LORA_UART->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)s_DmaRxBuf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralSRC;
    DMA_InitStructure.DMA_BufferSize = PORT_DMA_RX_SIZE;
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular; 
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);

    // DMA TX
    DMA_DeInit(DMA1_Channel2);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&LORA_UART->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)s_DmaTxBuf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = 0;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_Init(DMA1_Channel2, &DMA_InitStructure);

    DMA_Cmd(DMA1_Channel3, ENABLE); 
    USART_DMACmd(LORA_UART, USART_DMAReq_Rx | USART_DMAReq_Tx, ENABLE);
    USART_Cmd(LORA_UART, ENABLE);
    
    GPIO_WriteBit(GPIOA, GPIO_Pin_4, Bit_RESET); 
}

static uint32_t _STM32_GetTick(void)
{
    return GetTick(); 
}

static void _STM32_Phy_StartTx(const uint8_t *data, uint16_t len)
{
    if (len > PORT_DMA_TX_SIZE) len = PORT_DMA_TX_SIZE;
    memcpy(s_DmaTxBuf, data, len);
    DMA_Cmd(DMA1_Channel2, DISABLE);
    DMA1_Channel2->CNDTR = len;
    DMA_Cmd(DMA1_Channel2, ENABLE);
}

static bool _STM32_Phy_IsBusy(void)
{
    bool aux_busy = (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_5) == 1);
    bool dma_busy = (DMA_GetCurrDataCounter(DMA1_Channel2) != 0);
    return (aux_busy || dma_busy);
}

static uint32_t _STM32_Phy_GetRecoveryTime(void)
{
    return 50; 
}

static void _STM32_Phy_SetMode(bool config_mode)
{
    GPIO_WriteBit(GPIOA, GPIO_Pin_4, config_mode ? Bit_SET : Bit_RESET);
}

static void _STM32_Phy_HardReset(void)
{
}

static uint16_t _STM32_Phy_Read(uint8_t *buf, uint16_t max_len)
{
    uint16_t cnt = 0;
    uint16_t dma_write_idx = PORT_DMA_RX_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);
    
    while (s_RxReadIndex != dma_write_idx && cnt < max_len) {
        buf[cnt++] = s_DmaRxBuf[s_RxReadIndex++];
        if (s_RxReadIndex >= PORT_DMA_RX_SIZE) s_RxReadIndex = 0;
    }
    return cnt;
}

static void _STM32_Phy_ClearRx(void)
{
    s_RxReadIndex = PORT_DMA_RX_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);
}

// ============================================================
//                    3. 钩子绑定 (Binding)
// ============================================================

const LoRa_Port_Hooks_t g_LoRaPort = {
    .Init               = _STM32_Init,
    .GetTick            = _STM32_GetTick,
    .Phy_StartTx        = _STM32_Phy_StartTx,
    .Phy_IsBusy         = _STM32_Phy_IsBusy,
    .Phy_GetRecoveryTime= _STM32_Phy_GetRecoveryTime,
    .Phy_SetMode        = _STM32_Phy_SetMode,
    .Phy_HardReset      = _STM32_Phy_HardReset,
    .Phy_Read           = _STM32_Phy_Read,
    .Phy_ClearRx        = _STM32_Phy_ClearRx
};

void Port_Init_STM32(void)
{
    if (g_LoRaPort.Init) {
        g_LoRaPort.Init();
    }
}

// 暴露给 Service 层的辅助函数
void Port_ClearRxBuffer(void) {
    _STM32_Phy_ClearRx();
}

uint32_t Port_GetTick(void) {
    return _STM32_GetTick();
}
