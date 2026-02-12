#ifndef __SERIAL_H
#define __SERIAL_H

#include <stdint.h>
#include <stdio.h>

// ============================================================
//                    1. 配置宏
// ============================================================

// 发送缓冲区大小 (字节)
// 为了支持 LoRa 长包的 Hex 打印，建议至少设为 1024
#define SERIAL_TX_BUF_SIZE      1024

// 接收缓冲区大小 (字节)
// 用于接收 PC 发来的调试指令
#define SERIAL_RX_BUF_SIZE      128

// ============================================================
//                    2. 全局变量 (供 main.c 使用)
// ============================================================

extern char Serial_RxPacket[SERIAL_RX_BUF_SIZE]; // 接收包缓冲区
extern uint8_t Serial_RxFlag;                    // 接收完成标志 (1=收到完整包)

// ============================================================
//                    3. 核心接口
// ============================================================

/**
 * @brief  串口初始化 (DMA + RingBuffer)
 * @note   波特率 115200, PA9(TX), PA10(RX)
 */
void Serial_Init(void);

/**
 * @brief  非阻塞格式化打印
 * @param  fmt: 格式字符串 (用法同 printf)
 * @note   如果缓冲区满，新数据将被丢弃
 */
void Serial_Printf(const char *fmt, ...);

/**
 * @brief  非阻塞 Hex 数据转储
 * @param  tag:  标签前缀 (例如 "[RX]")
 * @param  data: 数据指针
 * @param  len:  数据长度
 * @note   输出格式: tag 01 02 03 ... (Len: N)\r\n
 */
void Serial_HexDump(const char *tag, const uint8_t *data, uint16_t len);

#endif // __SERIAL_H
