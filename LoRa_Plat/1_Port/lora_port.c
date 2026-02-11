#include "lora_port.h"
#include "stm32f10x.h"
#include <string.h>

#include "Delay.h"

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
    // 注意：如果 AUX 悬空，IPU 会导致读出高电平(忙)。确保模块连接良好。
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 3. USART3 配置 (默认 115200)
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

// [关键] 清空接收缓冲区
void Port_ClearRxBuffer(void) {
    // 将读指针直接同步到当前的写指针位置，相当于丢弃所有未读数据
    s_RxReadIndex = PORT_DMA_RX_BUF_SIZE - DMA_GetCurrDataCounter(DMA1_Channel3);
}

uint32_t Port_GetTick(void) {
    return GetTick(); 
}

// DMA TX 中断服务函数
void DMA1_Channel2_IRQHandler(void) {
    if (DMA_GetITStatus(DMA1_IT_TC2)) {
        DMA_ClearITPendingBit(DMA1_IT_TC2);
        s_TxDmaBusy = false;
    }
}


/**
  * @brief  获取随机数种子
  * @note   读取悬空引脚 PA0 (ADC1_IN0) 的底噪
  */
uint32_t Port_GetRandomSeed(void)
{
    // 1. 开启时钟
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_ADC1 | RCC_APB2Periph_GPIOA, ENABLE);
    
    // 2. 设置 ADC 分频 (必须 < 14MHz, 72/6 = 12MHz)
    RCC_ADCCLKConfig(RCC_PCLK2_Div6); 

    // 3. 配置 PA0 为模拟输入
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_0;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AIN;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // 4. 配置 ADC
    ADC_InitTypeDef ADC_InitStructure;
    ADC_InitStructure.ADC_Mode = ADC_Mode_Independent;
    ADC_InitStructure.ADC_ScanConvMode = DISABLE;
    ADC_InitStructure.ADC_ContinuousConvMode = DISABLE;
    ADC_InitStructure.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    ADC_InitStructure.ADC_DataAlign = ADC_DataAlign_Right;
    ADC_InitStructure.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &ADC_InitStructure);
    
    ADC_Cmd(ADC1, ENABLE);
    
    // 5. 校准
    ADC_ResetCalibration(ADC1);
    while(ADC_GetResetCalibrationStatus(ADC1));
    ADC_StartCalibration(ADC1);
    while(ADC_GetCalibrationStatus(ADC1));
    
    // 6. 采集噪声
    uint32_t seed = 0;
    // 采集多次，利用低位的抖动
    for(int i=0; i<32; i++) {
        ADC_RegularChannelConfig(ADC1, ADC_Channel_0, 1, ADC_SampleTime_1Cycles5); // 最快采样，捕捉噪声
        ADC_SoftwareStartConvCmd(ADC1, ENABLE);
        while(!ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC));
        
        uint16_t val = ADC_GetConversionValue(ADC1);
        // 只取最低位 (LSB)，因为它是噪声最大的部分
        if (val & 0x01) {
            seed |= (1 << i);
        }
        // 极短延时
        for(volatile int j=0; j<10; j++);
    }
    
    // 7. 关闭 ADC
    ADC_Cmd(ADC1, DISABLE);
    
    // 如果 PA0 接地了导致全是0，混入系统滴答计数作为保底
    if (seed == 0) seed = GetTick() ^ 0x5A5A5A5A;
    
    return seed;
}
