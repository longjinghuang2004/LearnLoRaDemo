#include "lora_port.h"
#include "stm32f10x.h"
#include "Delay.h" // 仅引用 GetTick，不使用 Delay_ms
#include <string.h>

// ============================================================
//                    1. 内部配置与变量
// ============================================================

// --- 硬件引脚定义 (ATK-LORA-01) ---
// UART3: PB10(TX), PB11(RX)
// CTRL:  PA4(MD0), PA5(AUX)
#define LORA_UART           USART3
#define LORA_UART_BAUD      115200

// --- DMA 缓冲区定义 ---
#define PORT_DMA_RX_SIZE    512
#define PORT_DMA_TX_SIZE    512

// 接收缓冲区 (DMA Circular Mode 自动写入)
static uint8_t  s_DmaRxBuf[PORT_DMA_RX_SIZE];
// 发送缓冲区 (DMA Normal Mode 读取)
static uint8_t  s_DmaTxBuf[PORT_DMA_TX_SIZE];

// 软件读指针 (追踪 DMA 接收进度)
static volatile uint16_t s_RxReadIndex = 0;

// ============================================================
//                    2. 私有函数实现 (STM32 Specific)
// ============================================================

static void _STM32_Init(void)
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
    USART_InitStructure.USART_BaudRate = LORA_UART_BAUD;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(LORA_UART, &USART_InitStructure);

    // 4. DMA RX (Circular Mode) -> DMA1_Channel3
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
    DMA_InitStructure.DMA_Mode = DMA_Mode_Circular; // 关键：循环模式
    DMA_InitStructure.DMA_Priority = DMA_Priority_High;
    DMA_InitStructure.DMA_M2M = DMA_M2M_Disable;
    DMA_Init(DMA1_Channel3, &DMA_InitStructure);

    // 5. DMA TX (Normal Mode) -> DMA1_Channel2
    DMA_DeInit(DMA1_Channel2);
    DMA_InitStructure.DMA_PeripheralBaseAddr = (uint32_t)&LORA_UART->DR;
    DMA_InitStructure.DMA_MemoryBaseAddr = (uint32_t)s_DmaTxBuf;
    DMA_InitStructure.DMA_DIR = DMA_DIR_PeripheralDST;
    DMA_InitStructure.DMA_BufferSize = 0;
    DMA_InitStructure.DMA_Mode = DMA_Mode_Normal;
    DMA_Init(DMA1_Channel2, &DMA_InitStructure);

    // 6. 启动
    DMA_Cmd(DMA1_Channel3, ENABLE); // 开启 RX DMA
    USART_DMACmd(LORA_UART, USART_DMAReq_Rx | USART_DMAReq_Tx, ENABLE);
    USART_Cmd(LORA_UART, ENABLE);
    
    // 默认状态
    GPIO_WriteBit(GPIOA, GPIO_Pin_4, Bit_RESET); // MD0=0 (透传)
}

static uint32_t _STM32_GetTick(void)
{
    return GetTick(); // 调用 System/Delay.c 中的实现
}

static void _STM32_Phy_StartTx(const uint8_t *data, uint16_t len)
{
    if (len > PORT_DMA_TX_SIZE) len = PORT_DMA_TX_SIZE;
    
    // 1. 填充数据到 DMA 缓冲区
    memcpy(s_DmaTxBuf, data, len);
    
    // 2. 启动 DMA 发送
    DMA_Cmd(DMA1_Channel2, DISABLE);
    DMA1_Channel2->CNDTR = len;
    DMA_Cmd(DMA1_Channel2, ENABLE);
}

static bool _STM32_Phy_IsBusy(void)
{
    // 忙碌定义：(AUX 引脚为高) 或 (DMA 发送计数器不为0)
    // 必须两个都检查，防止 DMA 刚发完但模块还没来得及拉高 AUX 的间隙
    
    bool aux_busy = (GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_5) == 1);
    bool dma_busy = (DMA_GetCurrDataCounter(DMA1_Channel2) != 0);
    
    return (aux_busy || dma_busy);
}

static uint32_t _STM32_Phy_GetRecoveryTime(void)
{
    // UART LoRa 模块在发送完成后，RF 切换回 RX 需要较长时间
    return 50; 
}

static void _STM32_Phy_SetMode(bool config_mode)
{
    // PA4: MD0
    GPIO_WriteBit(GPIOA, GPIO_Pin_4, config_mode ? Bit_SET : Bit_RESET);
}

static void _STM32_Phy_HardReset(void)
{
    // 如果有 RST 引脚连接，在这里操作
    // 目前假设未连接 RST，留空
}

static uint16_t _STM32_Phy_Read(uint8_t *buf, uint16_t max_len)
{
    uint16_t cnt = 0;
    // 计算 DMA 当前写到了哪里 (Total - Remaining)
    uint16_t dma_write_idx = PORT_DMA_RX_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);
    
    // 循环读取，直到读指针追上写指针
    while (s_RxReadIndex != dma_write_idx && cnt < max_len) {
        buf[cnt++] = s_DmaRxBuf[s_RxReadIndex++];
        if (s_RxReadIndex >= PORT_DMA_RX_SIZE) s_RxReadIndex = 0;
    }
    return cnt;
}

static void _STM32_Phy_ClearRx(void)
{
    // 将读指针直接同步到当前的写指针位置，丢弃所有未读数据
    s_RxReadIndex = PORT_DMA_RX_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);
}

// ============================================================
//                    3. 钩子绑定 (Binding)
// ============================================================

// 定义全局钩子实例
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

// 外部调用此函数来触发初始化
void Port_Init_STM32(void)
{
    if (g_LoRaPort.Init) {
        g_LoRaPort.Init();
    }
}
