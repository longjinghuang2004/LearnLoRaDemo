#ifndef __LORA_PROTOCOL_H
#define __LORA_PROTOCOL_H

#include "stm32f10x.h"

// --- 协议常量定义 ---
#define LORA_PACKET_HEADER_0    0xAA
#define LORA_PACKET_HEADER_1    0x55
#define LORA_PACKET_TAIL_0      0x0D // '\r'
#define LORA_PACKET_TAIL_1      0x0A // '\n'

#define MAX_CMD_LENGTH          32   // 指令最大长度
#define PACKET_MIN_SIZE         (2 + 1 + 1 + 2) // 头+长+校验+尾 (指令为空)

/**
 * @brief LoRa通信数据包结构体
 * @note  使用__packed确保结构体成员之间没有字节对齐填充，便于直接内存操作
 */
#pragma pack(1) // 1字节对齐
typedef struct {
    uint8_t header[2];      // 包头: 0xAA, 0x55
    uint8_t length;         // 指令长度
    char    command[MAX_CMD_LENGTH]; // 指令内容
    uint8_t checksum;       // 校验和
    uint8_t tail[2];        // 包尾: 0x0D, 0x0A
} LoRa_Packet_t;
#pragma pack() // 恢复默认对齐

#endif // __LORA_PROTOCOL_H
