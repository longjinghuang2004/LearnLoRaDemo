#include "Serial.h"
#include "stm32f10x.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

// ============================================================
//                    1. 内部变量 (私有化)
// ============================================================

// [修改] static
static char Serial_RxPacket[SERIAL_RX_BUF_SIZE];
static volatile uint8_t Serial_RxFlag = 0;
static uint16_t s_RxIndex = 0;

static uint8_t  s_TxBuf[SERIAL_TX_BUF_SIZE];
static volatile uint16_t s_TxHead = 0; 
static volatile uint16_t s_TxTail = 0; 
static volatile uint8_t  s_DmaBusy = 0; 
static volatile uint16_t s_LastSendLen = 0; 

// ============================================================
//                    2. 内部辅助函数
// ============================================================

static void _CheckAndStartDMA(void)
{
    if (s_DmaBusy) return;       
    if (s_TxHead == s_TxTail) return; 

    uint16_t sendLen;
    uint16_t head = s_TxHead;
    uint16_t tail = s_TxTail;

    if (head > tail) {
        sendLen = head - tail;
    } else {
        sendLen = SERIAL_TX_BUF_SIZE - tail;
    }

    s_LastSendLen = sendLen;
    s_DmaBusy = 1;

    DMA_Cmd(DMA1_Channel4, DISABLE);
    DMA1_Channel4->CMAR = (uint32_t)&s_TxBuf[tail];
    DMA1_Channel4->CNDTR = sendLen;
    DMA_Cmd(DMA1_Channel4, ENABLE);
}

static int _WriteToRingBuffer(const uint8_t *data, uint16_t len)
{
    if (len == 0 || len > SERIAL_TX_BUF_SIZE) return 0;

    uint16_t head = s_TxHead;
    uint16_t tail = s_TxTail;
    
    uint16_t used = (head >= tail) ? (head - tail) : (SERIAL_TX_BUF_SIZE + head - tail);
    uint16_t free = SERIAL_TX_BUF_SIZE - 1 - used;

    if (len > free) return 0; 

    uint16_t chunk1 = SERIAL_TX_BUF_SIZE - head;
    if (len <= chunk1) {
        memcpy(&s_TxBuf[head], data, len);
        s_TxHead = (head + len) % SERIAL_TX_BUF_SIZE;
    } else {
        memcpy(&s_TxBuf[head], data, chunk1);
        memcpy(&s_TxBuf[0], data + chunk1, len - chunk1);
        s_TxHead = len - chunk1;
    }

    __disable_irq();
    _CheckAndStartDMA();
    __enable_irq();
    
    return 1; 
}

// ============================================================
//                    3. 公开接口实现
// ============================================================

void Serial_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &USART_InitStructure);

    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2; 
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE);
    
    DMA_InitTypeDef DMA_InitStructure;
    DMA_DeInit(DMA1_Channel4);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)s_TxBuf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = 0; 
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal; 
    DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel4, &DMA_InitStructure);
    
    DMA_ITConfig(DMA1_Channel4, DMA_IT_TC, ENABLE); 

    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2; 
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    USART_Cmd(USART1, ENABLE);
}

void Serial_Printf(const char *fmt, ...)
{
    char buf[256]; 
    va_list args;
    
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    if (len > 0) {
        _WriteToRingBuffer((uint8_t*)buf, len);
    }
}

void Serial_HexDump(const char *tag, const uint8_t *data, uint16_t len)
{
    Serial_Printf("%s ", tag);
    
    char hex_buf[48]; 
    uint16_t i = 0;
    
    while (i < len) {
        uint16_t chunk = (len - i > 16) ? 16 : (len - i);
        int pos = 0;
        for (uint16_t j = 0; j < chunk; j++) {
            pos += sprintf(&hex_buf[pos], "%02X ", data[i + j]);
        }
        _WriteToRingBuffer((uint8_t*)hex_buf, pos);
        i += chunk;
    }
    
    Serial_Printf("(Len: %d)\r\n", len);
}

int fputc(int ch, FILE *f)
{
    uint8_t c = (uint8_t)ch;
    _WriteToRingBuffer(&c, 1);
    return ch;
}

// [新增] 获取接收包接口
bool Serial_GetRxPacket(char *buf, uint16_t max_len) {
    if (Serial_RxFlag == 1) {
        // 简单的临界区保护，防止读取时被中断修改
        __disable_irq();
        strncpy(buf, Serial_RxPacket, max_len - 1);
        buf[max_len - 1] = '\0'; // 确保结尾
        Serial_RxFlag = 0;
        __enable_irq();
        return true;
    }
    return false;
}

// ============================================================
//                    4. 中断服务函数
// ============================================================

void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
    {
        uint8_t RxData = USART_ReceiveData(USART1);
        
        if (Serial_RxFlag == 0)
        {
            if (RxData == '\n' || RxData == '\r')
            {
                if (s_RxIndex > 0) {
                    Serial_RxPacket[s_RxIndex] = '\0';
                    Serial_RxFlag = 1; 
                    s_RxIndex = 0;
                }
            }
            else
            {
                Serial_RxPacket[s_RxIndex] = RxData;
                s_RxIndex++;
                if (s_RxIndex >= SERIAL_RX_BUF_SIZE - 1) {
                    s_RxIndex = SERIAL_RX_BUF_SIZE - 2; 
                }
            }
        }
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}

void DMA1_Channel4_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA1_IT_TC4))
    {
        DMA_ClearITPendingBit(DMA1_IT_TC4);
        
        s_TxTail = (s_TxTail + s_LastSendLen) % SERIAL_TX_BUF_SIZE;
        s_DmaBusy = 0;
        
        _CheckAndStartDMA();
    }
}
