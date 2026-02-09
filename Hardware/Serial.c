#include "Serial.h"
#include "stm32f10x.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

// --- 配置 ---
#define DEBUG_BAUDRATE      115200
#define TX_BUF_SIZE         512

// --- 全局变量定义 (分配内存) ---
char Serial_RxPacket[SERIAL_RX_BUFFER_SIZE]; // 接收数据包缓冲区
uint8_t Serial_RxFlag = 0;                   // 接收完成标志 (1=收到完整包)
static uint8_t s_RxIndex = 0;                // 内部接收索引

// --- 发送相关变量 ---
static uint8_t  s_TxBuf[TX_BUF_SIZE];
static volatile uint16_t s_TxHead = 0;
static volatile uint16_t s_TxTail = 0;
static volatile uint8_t  s_DmaBusy = 0;
static volatile uint16_t s_LastSendLen = 0;

// --- 内部函数声明 ---
static void _CheckAndStartDMA(void);

void Serial_Init(void)
{
    // 1. 开启时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    // 2. GPIO 配置
    GPIO_InitTypeDef GPIO_InitStructure;
    // PA9 TX (复用推挽)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // PA10 RX (浮空输入或上拉输入)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 3. USART 配置
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = DEBUG_BAUDRATE;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &USART_InitStructure);

    // 4. 中断配置 (关键修复：开启接收中断)
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    NVIC_InitTypeDef NVIC_InitStructure;
    
    // 配置 USART1 中断 (用于接收)
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 5. DMA TX 配置
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

    // 配置 DMA 中断
    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 6. 使能串口
    USART_Cmd(USART1, ENABLE);
}

// --- 接收中断服务函数 ---
// 当 PC 发送数据给 STM32 时触发
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
    {
        uint8_t RxData = USART_ReceiveData(USART1);
        
        // 如果之前的包还没处理完，为了防止覆盖，这里简单丢弃新数据
        // 或者你可以选择覆盖旧数据
        if (Serial_RxFlag == 0)
        {
            // 简单的文本协议：以回车换行结束
            if (RxData == '\n' || RxData == '\r')
            {
                // 收到结束符，且缓冲区有数据
                if (s_RxIndex > 0)
                {
                    Serial_RxPacket[s_RxIndex] = '\0'; // 添加字符串结束符
                    Serial_RxFlag = 1; // 置标志位，通知 main 循环处理
                    s_RxIndex = 0;     // 重置索引
                }
            }
            else
            {
                // 正常字符，存入缓冲区
                Serial_RxPacket[s_RxIndex] = RxData;
                s_RxIndex++;
                
                // 防止缓冲区溢出
                if (s_RxIndex >= SERIAL_RX_BUFFER_SIZE)
                {
                    s_RxIndex = SERIAL_RX_BUFFER_SIZE - 1;
                }
            }
        }
        
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}

// --- 以下是 DMA 发送相关逻辑 (保持不变) ---

static void _CheckAndStartDMA(void)
{
    if (s_DmaBusy) return;
    if (s_TxHead == s_TxTail) return;

    uint16_t sendLen;
    uint16_t head = s_TxHead;
    uint16_t tail = s_TxTail;

    if (head > tail) sendLen = head - tail;
    else sendLen = TX_BUF_SIZE - tail;

    s_LastSendLen = sendLen;
    s_DmaBusy = 1;

    DMA_Cmd(DMA1_Channel4, DISABLE);
    DMA1_Channel4->CMAR = (uint32_t)&s_TxBuf[tail];
    DMA1_Channel4->CNDTR = sendLen;
    DMA_Cmd(DMA1_Channel4, ENABLE);
}

int USART_DMA_Send(uint8_t *data, uint16_t len)
{
    if (len == 0 || len > TX_BUF_SIZE) return 0;

    uint16_t head = s_TxHead;
    uint16_t tail = s_TxTail;
    uint16_t used = (head >= tail) ? (head - tail) : (TX_BUF_SIZE + head - tail);
    uint16_t free = TX_BUF_SIZE - 1 - used;

    if (len > free) return 0; 

    uint16_t chunk1 = TX_BUF_SIZE - head;
    if (len <= chunk1) {
        memcpy(&s_TxBuf[head], data, len);
        s_TxHead = (head + len) % TX_BUF_SIZE;
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

int USART_DMA_Printf(const char *fmt, ...)
{
    char buf[128];
    va_list args;
    va_start(args, fmt);
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    if (len > 0) return USART_DMA_Send((uint8_t*)buf, len);
    return 0;
}

int fputc(int ch, FILE *f)
{
    uint8_t c = (uint8_t)ch;
    USART_DMA_Send(&c, 1);
    return ch;
}

void DMA1_Channel4_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA1_IT_TC4))
    {
        DMA_ClearITPendingBit(DMA1_IT_TC4);
        s_TxTail = (s_TxTail + s_LastSendLen) % TX_BUF_SIZE;
        s_DmaBusy = 0;
        _CheckAndStartDMA();
    }
}
