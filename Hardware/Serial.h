#ifndef __SERIAL_H
#define __SERIAL_H

#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

// ============================================================
//                    1. 配置宏
// ============================================================

#define SERIAL_TX_BUF_SIZE      1024
#define SERIAL_RX_BUF_SIZE      128

// ============================================================
//                    2. 核心接口
// ============================================================

void Serial_Init(void);
void Serial_Printf(const char *fmt, ...);
void Serial_HexDump(const char *tag, const uint8_t *data, uint16_t len);

// [新增] 获取接收到的数据包 (原子操作)
// 如果有数据包，将其复制到 buf 并返回 true，同时清空内部标志
bool Serial_GetRxPacket(char *buf, uint16_t max_len);

#endif // __SERIAL_H
