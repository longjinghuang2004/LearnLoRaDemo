/**
  ******************************************************************************
  * @file    lora_ring_buffer.h
  * @author  LoRaPlat Team
  * @brief   通用环形缓冲区 (Ring Buffer) 接口定义
  *          纯逻辑实现，无硬件依赖，内存由使用者提供。
  ******************************************************************************
  */

#ifndef __LORA_RING_BUFFER_H
#define __LORA_RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

// ============================================================
//                    1. 数据结构定义
// ============================================================

typedef struct {
    uint8_t  *pBuffer;   // 指向外部提供的缓冲区数组
    uint16_t Size;       // 缓冲区总大小
    uint16_t Head;       // 写指针 (Write Index)
    uint16_t Tail;       // 读指针 (Read Index)
    uint16_t Count;      // 当前数据量
} LoRa_RingBuffer_t;

// ============================================================
//                    2. 核心接口
// ============================================================

/**
 * @brief  初始化环形缓冲区
 * @param  rb: 句柄
 * @param  buffer: 外部数组指针
 * @param  size: 数组大小
 */
void LoRa_RingBuffer_Init(LoRa_RingBuffer_t *rb, uint8_t *buffer, uint16_t size);

/**
 * @brief  写入数据 (Push)
 * @param  rb: 句柄
 * @param  data: 数据源
 * @param  length: 写入长度
 * @return 实际写入长度 (若空间不足可能小于 length)
 */
uint16_t LoRa_RingBuffer_Write(LoRa_RingBuffer_t *rb, const uint8_t *data, uint16_t length);

/**
 * @brief  读取数据 (Pop)
 * @param  rb: 句柄
 * @param  data: 目标缓冲区
 * @param  max_length: 最大读取长度
 * @return 实际读取长度
 */
uint16_t LoRa_RingBuffer_Read(LoRa_RingBuffer_t *rb, uint8_t *data, uint16_t max_length);

/**
 * @brief  清空缓冲区
 */
void LoRa_RingBuffer_Clear(LoRa_RingBuffer_t *rb);

// ============================================================
//                    3. 状态查询
// ============================================================

uint16_t LoRa_RingBuffer_GetCount(const LoRa_RingBuffer_t *rb);
uint16_t LoRa_RingBuffer_GetFree(const LoRa_RingBuffer_t *rb);
bool     LoRa_RingBuffer_IsEmpty(const LoRa_RingBuffer_t *rb);
bool     LoRa_RingBuffer_IsFull(const LoRa_RingBuffer_t *rb);

#endif // __LORA_RING_BUFFER_H
