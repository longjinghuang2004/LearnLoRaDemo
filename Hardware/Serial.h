#ifndef __SERIAL_H
#define __SERIAL_H

#include <stdio.h>
#include <stdint.h> // [修复] 包含此头文件以定义 uint8_t, uint16_t 等类型

// --- 缓冲区大小宏定义 ---
#define SERIAL_RX_BUFFER_SIZE 128

// --- 全局变量声明 (供外部使用) ---
// 这里的 extern 告诉编译器：这些变量在 Serial.c 里定义了，你去那里找
extern char Serial_RxPacket[];
extern uint8_t Serial_RxFlag;

// 宏定义替换，让 printf 指向我们的非阻塞函数
#define Serial_Printf  USART_DMA_Printf

// --- 函数声明 ---
void Serial_Init(void);
int USART_DMA_Printf(const char *fmt, ...);

#endif
