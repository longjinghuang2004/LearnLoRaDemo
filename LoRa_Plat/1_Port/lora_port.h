#ifndef __LORA_PORT_H
#define __LORA_PORT_H

#include <stdint.h>
#include <stdbool.h>

// 硬件相关宏定义
#define LORA_UART_BAUDRATE  115200

// --- 接口声明 ---

// 端口初始化 (GPIO, UART, DMA, NVIC)
void Port_Init(void);

// 基础 IO 控制
void Port_SetMD0(bool level);   // true=High(Config), false=Low(Comm)
bool Port_GetAUX(void);         // true=High(Busy), false=Low(Idle)
void Port_SetRST(bool level);   // true=High, false=Low(Reset active)

// 数据收发 (对接 DMA)
// 返回实际写入 DMA 缓冲区的字节数
uint16_t Port_WriteData(const uint8_t *data, uint16_t len);

// 从 DMA 循环缓冲区读取数据
// 返回实际读取的字节数
uint16_t Port_ReadData(uint8_t *buf, uint16_t max_len);

// 清空底层接收缓冲区
void Port_ClearRxBuffer(void);

// 系统时基 (ms)
uint32_t Port_GetTick(void);

uint32_t Port_GetRandomSeed(void);

#endif
