#ifndef __SERIAL_H
#define __SERIAL_H

#include <stdio.h>
#include <stdint.h> // [修复] 包含此头文件以定义 uint8_t, uint16_t 等类型

// --- 缓冲区大小宏定义 ---
#define SERIAL_RX_BUFFER_SIZE 1024

// --- 全局变量声明 ---
extern char Serial_RxPacket[];
extern uint8_t Serial_RxFlag;

// --- [修正] 使用正确的宏定义来替换 Serial_Printf ---
// 这将确保所有地方的调用都指向正确的、内存安全的标准 printf
#define Serial_Printf printf

// --- 函数原型声明 ---
void Serial_Init(void);
void Serial_SendByte(uint8_t Byte);
void Serial_SendArray(uint8_t *Array, uint16_t Length);
void Serial_SendString(char *String);
void Serial_SendNumber(uint32_t Number, uint8_t Length);

#endif
