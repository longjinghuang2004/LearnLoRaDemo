/**
  ******************************************************************************
  * @file    lora_ring_buffer.c
  * @author  LoRaPlat Team
  * @brief   通用环形缓冲区实现
  ******************************************************************************
  */

#include "lora_ring_buffer.h"
#include <string.h> // memcpy

// ============================================================
//                    1. 核心接口实现
// ============================================================

void LoRa_RingBuffer_Init(LoRa_RingBuffer_t *rb, uint8_t *buffer, uint16_t size) {
    if (!rb || !buffer || size == 0) return;
    
    rb->pBuffer = buffer;
    rb->Size    = size;
    rb->Head    = 0;
    rb->Tail    = 0;
    rb->Count   = 0;
}

uint16_t LoRa_RingBuffer_Write(LoRa_RingBuffer_t *rb, const uint8_t *data, uint16_t length) {
    if (!rb || !data || length == 0) return 0;
    
    uint16_t free_space = rb->Size - rb->Count;
    if (length > free_space) length = free_space; // 截断写入
    
    if (length == 0) return 0;

    uint16_t chunk1 = rb->Size - rb->Head;
    
    if (length <= chunk1) {
        // 情况1: 直接写入，不需要回绕
        memcpy(&rb->pBuffer[rb->Head], data, length);
        rb->Head += length;
        if (rb->Head == rb->Size) rb->Head = 0;
    } else {
        // 情况2: 需要回绕
        memcpy(&rb->pBuffer[rb->Head], data, chunk1);
        memcpy(&rb->pBuffer[0], data + chunk1, length - chunk1);
        rb->Head = length - chunk1;
    }
    
    rb->Count += length;
    return length;
}

uint16_t LoRa_RingBuffer_Read(LoRa_RingBuffer_t *rb, uint8_t *data, uint16_t max_length) {
    if (!rb || !data || max_length == 0 || rb->Count == 0) return 0;
    
    if (max_length > rb->Count) max_length = rb->Count;
    
    uint16_t chunk1 = rb->Size - rb->Tail;
    
    if (max_length <= chunk1) {
        // 情况1: 直接读取，不需要回绕
        memcpy(data, &rb->pBuffer[rb->Tail], max_length);
        rb->Tail += max_length;
        if (rb->Tail == rb->Size) rb->Tail = 0;
    } else {
        // 情况2: 需要回绕
        memcpy(data, &rb->pBuffer[rb->Tail], chunk1);
        memcpy(data + chunk1, &rb->pBuffer[0], max_length - chunk1);
        rb->Tail = max_length - chunk1;
    }
    
    rb->Count -= max_length;
    return max_length;
}

void LoRa_RingBuffer_Clear(LoRa_RingBuffer_t *rb) {
    if (!rb) return;
    rb->Head = 0;
    rb->Tail = 0;
    rb->Count = 0;
}

// ============================================================
//                    2. 状态查询实现
// ============================================================

uint16_t LoRa_RingBuffer_GetCount(const LoRa_RingBuffer_t *rb) {
    return rb ? rb->Count : 0;
}

uint16_t LoRa_RingBuffer_GetFree(const LoRa_RingBuffer_t *rb) {
    return rb ? (rb->Size - rb->Count) : 0;
}

bool LoRa_RingBuffer_IsEmpty(const LoRa_RingBuffer_t *rb) {
    return (rb && rb->Count == 0);
}

bool LoRa_RingBuffer_IsFull(const LoRa_RingBuffer_t *rb) {
    return (rb && rb->Count == rb->Size);
}
