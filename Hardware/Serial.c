#include "Serial.h"
#include "stm32f10x.h"
#include <stdarg.h>
#include <string.h>
#include <stdio.h>

// ============================================================
//                    1. 内部变量与定义
// ============================================================

// 接收相关
char Serial_RxPacket[SERIAL_RX_BUF_SIZE];
uint8_t Serial_RxFlag = 0;
static uint16_t s_RxIndex = 0;

// 发送相关 (Ring Buffer)
static uint8_t  s_TxBuf[SERIAL_TX_BUF_SIZE];
static volatile uint16_t s_TxHead = 0; // 写指针
static volatile uint16_t s_TxTail = 0; // 读指针 (DMA读取位置)
static volatile uint8_t  s_DmaBusy = 0; // DMA 忙碌标志
static volatile uint16_t s_LastSendLen = 0; // 上次 DMA 传输长度

// ============================================================
//                    2. 内部辅助函数
// ============================================================

// 检查并启动 DMA (如果空闲且有数据)
static void _CheckAndStartDMA(void)
{
    if (s_DmaBusy) return;       // DMA 正在忙，退出
    if (s_TxHead == s_TxTail) return; // 缓冲区空，退出

    uint16_t sendLen;
    uint16_t head = s_TxHead;
    uint16_t tail = s_TxTail;

    // 计算本次可以连续发送的长度 (处理环形回卷)
    if (head > tail) {
        sendLen = head - tail;
    } else {
        sendLen = SERIAL_TX_BUF_SIZE - tail;
    }

    s_LastSendLen = sendLen;
    s_DmaBusy = 1;

    // 配置 DMA1_Channel4 (USART1_TX)
    DMA_Cmd(DMA1_Channel4, DISABLE);
    DMA1_Channel4->CMAR = (uint32_t)&s_TxBuf[tail];
    DMA1_Channel4->CNDTR = sendLen;
    DMA_Cmd(DMA1_Channel4, ENABLE);
}

// 将数据写入环形缓冲区 (核心非阻塞逻辑)
static int _WriteToRingBuffer(const uint8_t *data, uint16_t len)
{
    if (len == 0 || len > SERIAL_TX_BUF_SIZE) return 0;

    uint16_t head = s_TxHead;
    uint16_t tail = s_TxTail;
    
    // 计算剩余空间
    // 环形缓冲区公式: Free = Size - 1 - Used
    uint16_t used = (head >= tail) ? (head - tail) : (SERIAL_TX_BUF_SIZE + head - tail);
    uint16_t free = SERIAL_TX_BUF_SIZE - 1 - used;

    // [策略 Q4-A] 空间不足直接丢弃，保证不阻塞
    if (len > free) return 0; 

    // 写入数据 (处理回卷)
    uint16_t chunk1 = SERIAL_TX_BUF_SIZE - head;
    if (len <= chunk1) {
        // 情况1: 不需要回卷，直接写入
        memcpy(&s_TxBuf[head], data, len);
        s_TxHead = (head + len) % SERIAL_TX_BUF_SIZE;
    } else {
        // 情况2: 需要回卷，分两段写入
        memcpy(&s_TxBuf[head], data, chunk1);
        memcpy(&s_TxBuf[0], data + chunk1, len - chunk1);
        s_TxHead = len - chunk1;
    }

    // 尝试启动 DMA
    // 关中断保护，防止此时 DMA 中断触发导致状态竞争
    __disable_irq();
    _CheckAndStartDMA();
    __enable_irq();
    
    return 1; // 写入成功
}

// ============================================================
//                    3. 公开接口实现
// ============================================================

void Serial_Init(void)
{
    // 1. 时钟使能
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1 | RCC_APB2Periph_GPIOA, ENABLE);
    RCC_AHBPeriphClockCmd(RCC_AHBPeriph_DMA1, ENABLE);

    // 2. GPIO 配置
    GPIO_InitTypeDef GPIO_InitStructure;
    // PA9 TX (复用推挽)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // PA10 RX (上拉输入)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 3. USART 配置 (115200, 8N1)
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = 115200;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &USART_InitStructure);

    // 4. 中断配置 (RXNE 用于接收 PC 指令)
    USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

    NVIC_InitTypeDef NVIC_InitStructure;
    NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2; // 优先级低于 LoRa 业务
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 0;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 5. DMA TX 配置 (DMA1_Channel4)
    USART_DMACmd(USART1, USART_DMAReq_Tx, ENABLE);
    
    DMA_InitTypeDef DMA_InitStructure;
    DMA_DeInit(DMA1_Channel4);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&USART1->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)s_TxBuf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = 0; // 初始为0
    DMA_InitStructure.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
    DMA_InitStructure.DMA_MemoryInc = DMA_MemoryInc_Enable;
    DMA_InitStructure.DMA_PeripheralDataSize = DMA_PeripheralDataSize_Byte;
    DMA_InitStructure.DMA_MemoryDataSize = DMA_MemoryDataSize_Byte;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal; // 普通模式，每次发一段
    DMA_InitStructure.DMA_Priority = DMA_Priority_Medium;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel4, &DMA_InitStructure);
    
    DMA_ITConfig(DMA1_Channel4, DMA_IT_TC, ENABLE); // 开启传输完成中断

    // DMA 中断配置
    NVIC_InitStructure.NVIC_IRQChannel = DMA1_Channel4_IRQn;
    NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 2; // 与串口中断同级
    NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
    NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
    NVIC_Init(&NVIC_InitStructure);

    // 6. 使能串口
    USART_Cmd(USART1, ENABLE);
}

void Serial_Printf(const char *fmt, ...)
{
    char buf[256]; // 临时缓冲区，用于格式化单条日志
    va_list args;
    
    va_start(args, fmt);
    // 使用 vsnprintf 防止缓冲区溢出
    int len = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);
    
    if (len > 0) {
        _WriteToRingBuffer((uint8_t*)buf, len);
    }
}

void Serial_HexDump(const char *tag, const uint8_t *data, uint16_t len)
{
    // 1. 打印 Header: "[TAG] "
    Serial_Printf("%s ", tag);
    
    // 2. 循环打印 Hex 数据
    // 为了避免占用巨大栈空间，我们分块格式化，每块 16 字节
    char hex_buf[48]; // 16字节 * 3字符("XX ") + 1
    uint16_t i = 0;
    
    while (i < len) {
        uint16_t chunk = (len - i > 16) ? 16 : (len - i);
        int pos = 0;
        for (uint16_t j = 0; j < chunk; j++) {
            pos += sprintf(&hex_buf[pos], "%02X ", data[i + j]);
        }
        // 写入这一块
        _WriteToRingBuffer((uint8_t*)hex_buf, pos);
        i += chunk;
    }
    
    // 3. 打印 Tail: "(Len: N)\r\n"
    Serial_Printf("(Len: %d)\r\n", len);
}

// 重定向标准库 printf (可选)
int fputc(int ch, FILE *f)
{
    uint8_t c = (uint8_t)ch;
    _WriteToRingBuffer(&c, 1);
    return ch;
}

// ============================================================
//                    4. 中断服务函数
// ============================================================

// 串口接收中断 (处理 PC 指令)
void USART1_IRQHandler(void)
{
    if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
    {
        uint8_t RxData = USART_ReceiveData(USART1);
        
        if (Serial_RxFlag == 0)
        {
            // 简单的行缓冲协议
            if (RxData == '\n' || RxData == '\r')
            {
                if (s_RxIndex > 0) {
                    Serial_RxPacket[s_RxIndex] = '\0';
                    Serial_RxFlag = 1; // 通知 main 处理
                    s_RxIndex = 0;
                }
            }
            else
            {
                Serial_RxPacket[s_RxIndex] = RxData;
                s_RxIndex++;
                // [修复 V3.0.1 Bug] 防止缓冲区溢出
                if (s_RxIndex >= SERIAL_RX_BUF_SIZE - 1) {
                    s_RxIndex = SERIAL_RX_BUF_SIZE - 2; 
                }
            }
        }
        USART_ClearITPendingBit(USART1, USART_IT_RXNE);
    }
}

// DMA 发送完成中断
void DMA1_Channel4_IRQHandler(void)
{
    if (DMA_GetITStatus(DMA1_IT_TC4))
    {
        DMA_ClearITPendingBit(DMA1_IT_TC4);
        
        // 更新读指针 (Tail)
        s_TxTail = (s_TxTail + s_LastSendLen) % SERIAL_TX_BUF_SIZE;
        s_DmaBusy = 0;
        
        // 检查是否还有剩余数据需要发送
        _CheckAndStartDMA();
    }
}
