#include "Serial.h"
#include "stm32f10x.h"
#include <stdio.h>
// #include <stdarg.h> // 不再需要，因为 vsprintf 已被移除

// --- 全局变量定义 ---
char Serial_RxPacket[SERIAL_RX_BUFFER_SIZE];
uint8_t Serial_RxFlag;

/**
  * 函    数：串口初始化
  * (此函数无变化)
  */
void Serial_Init(void)
{
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_USART1, ENABLE);
	RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
	
	GPIO_InitTypeDef GPIO_InitStructure;
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_AF_PP;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_9;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
	GPIO_InitStructure.GPIO_Pin = GPIO_Pin_10;
	GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
	GPIO_Init(GPIOA, &GPIO_InitStructure);
	
	USART_InitTypeDef USART_InitStructure;
	USART_InitStructure.USART_BaudRate = 115200;
	USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
	USART_InitStructure.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
	USART_InitStructure.USART_Parity = USART_Parity_No;
	USART_InitStructure.USART_StopBits = USART_StopBits_1;
	USART_InitStructure.USART_WordLength = USART_WordLength_8b;
	USART_Init(USART1, &USART_InitStructure);
	
	USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);
	
	NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
	
	NVIC_InitTypeDef NVIC_InitStructure;
	NVIC_InitStructure.NVIC_IRQChannel = USART1_IRQn;
	NVIC_InitStructure.NVIC_IRQChannelCmd = ENABLE;
	NVIC_InitStructure.NVIC_IRQChannelPreemptionPriority = 1;
	NVIC_InitStructure.NVIC_IRQChannelSubPriority = 1;
	NVIC_Init(&NVIC_InitStructure);
	
	USART_Cmd(USART1, ENABLE);
}

/**
  * 函    数：串口发送一个字节
  * (此函数无变化)
  */
void Serial_SendByte(uint8_t Byte)
{
	USART_SendData(USART1, Byte);
	while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET);
}

/**
  * 函    数：串口发送一个字符串
  * (此函数无变化)
  */
void Serial_SendString(char *String)
{
	for (uint16_t i = 0; String[i] != '\0'; i++)
	{
		Serial_SendByte(String[i]);
	}
}

// ... 其他基础发送函数 (SendArray, SendNumber) 保持不变 ...

/**
  * 函    数：重定向C语言标准库的printf到底层串口发送函数
  * 参    数：ch - 要打印的字符
  * 参    数：f - 文件流指针 (未使用)
  * 返 回 值：返回写入的字符
  * 说    明：这是实现标准printf()的关键。标准库的printf会逐个字符
  *           调用本函数，本函数再通过Serial_SendByte将字符发送出去。
  *           这种“流式”处理方式内存效率极高，且没有缓冲区大小限制。
  */
int fputc(int ch, FILE *f)
{
	Serial_SendByte(ch);
	return ch;
}

/*
 * 自定义的 printf 函数已被完全移除。
 * 它存在严重的堆栈溢出风险且效率低下。
 * 请在所有代码中直接使用标准库的 printf() 函数。
 */

/**
  * 函    数：USART1中断函数
  * (此函数无变化，保持上一版修复后的状态)
  */
void USART1_IRQHandler(void)
{
	static uint16_t pRxPacket = 0;
	static uint8_t RxState = 0;
	
	if (USART_GetITStatus(USART1, USART_IT_RXNE) == SET)
	{
		uint8_t RxData = USART_ReceiveData(USART1);
		
		if (RxState == 0)
		{
			if (RxData == '\r')
			{
				RxState = 1;
			}
			else
			{
				Serial_RxPacket[pRxPacket] = RxData;
				pRxPacket++;
                if (pRxPacket >= (SERIAL_RX_BUFFER_SIZE - 1))
                {
                    pRxPacket = SERIAL_RX_BUFFER_SIZE - 1;
                }
			}
		}
		else if (RxState == 1)
		{
			if (RxData == '\n')
			{
				RxState = 0;
				Serial_RxPacket[pRxPacket] = '\0';
				Serial_RxFlag = 1;
				pRxPacket = 0;
			}
			else
			{
				RxState = 0;
                if (pRxPacket < (SERIAL_RX_BUFFER_SIZE - 1)) {
                    Serial_RxPacket[pRxPacket++] = '\r';
                }
                if (pRxPacket < (SERIAL_RX_BUFFER_SIZE - 1)) {
                    Serial_RxPacket[pRxPacket++] = RxData;
                }
			}
		}
		
		USART_ClearITPendingBit(USART1, USART_IT_RXNE);
	}
}
