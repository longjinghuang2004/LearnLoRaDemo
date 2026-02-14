#include "lora_manager_buffer.h"
#include "lora_ring_buffer.h"
#include "lora_port.h"
#include "LoRaPlatConfig.h"
#include <string.h>

#define TX_QUEUE_SIZE   MGR_TX_BUF_SIZE
#define RX_QUEUE_SIZE   MGR_RX_BUF_SIZE

static uint8_t s_TxBufArr[TX_QUEUE_SIZE];
static uint8_t s_RxBufArr[RX_QUEUE_SIZE];
static LoRa_RingBuffer_t s_TxRing;
static LoRa_RingBuffer_t s_RxRing;

void LoRa_Manager_Buffer_Init(void) {
    LoRa_RingBuffer_Init(&s_TxRing, s_TxBufArr, TX_QUEUE_SIZE);
    LoRa_RingBuffer_Init(&s_RxRing, s_RxBufArr, RX_QUEUE_SIZE);
}

// 【优化】使用外部传入的 scratch_buf，移除内部 static/local 数组
bool LoRa_Manager_Buffer_PushTx(const LoRa_Packet_t *packet, uint8_t tmode, uint8_t channel, 
                                uint8_t *scratch_buf, uint16_t scratch_len) {
    if (!scratch_buf) return false;

    // 1. 序列化到共享缓冲区
    uint16_t len = LoRa_Manager_Protocol_Pack(packet, scratch_buf, scratch_len, tmode, channel);
    if (len == 0) return false;
    
    // 2. 检查队列空间
    if (LoRa_RingBuffer_GetFree(&s_TxRing) < len) return false;
    
    // 3. 写入队列
    LoRa_RingBuffer_Write(&s_TxRing, scratch_buf, len);
    return true;
}

bool LoRa_Manager_Buffer_HasTxData(void) {
    return !LoRa_RingBuffer_IsEmpty(&s_TxRing);
}

// 【优化】直接使用传入的 buffer 作为输出目标
uint16_t LoRa_Manager_Buffer_PeekTx(uint8_t *scratch_buf, uint16_t scratch_len) {
    if (s_TxRing.Count == 0) return 0;
    
    uint16_t len = s_TxRing.Count;
    if (len > scratch_len) len = scratch_len;
    
    uint16_t chunk1 = s_TxRing.Size - s_TxRing.Tail;
    if (len <= chunk1) {
        memcpy(scratch_buf, &s_TxRing.pBuffer[s_TxRing.Tail], len);
    } else {
        memcpy(scratch_buf, &s_TxRing.pBuffer[s_TxRing.Tail], chunk1);
        memcpy(scratch_buf + chunk1, &s_TxRing.pBuffer[0], len - chunk1);
    }
    return len;
}

void LoRa_Manager_Buffer_PopTx(uint16_t len) {
    uint8_t dummy;
    // 简单粗暴的移动指针方式，虽然低效但安全
    for(uint16_t i=0; i<len; i++) {
        LoRa_RingBuffer_Read(&s_TxRing, &dummy, 1);
    }
}

uint16_t LoRa_Manager_Buffer_PullFromPort(void) {
    // 这里可以使用小一点的栈变量，因为是从 DMA 读，通常不会太大
    uint8_t temp_buf[64]; 
    uint16_t total_read = 0;
    
    while (1) {
        uint16_t len = LoRa_Port_ReceiveData(temp_buf, sizeof(temp_buf));
        if (len == 0) break;
        LoRa_RingBuffer_Write(&s_RxRing, temp_buf, len);
        total_read += len;
    }
    return total_read;
}

// 【优化】使用外部传入的 scratch_buf 进行 Peek 和 Unpack
bool LoRa_Manager_Buffer_GetRxPacket(LoRa_Packet_t *packet, uint16_t local_id, uint16_t group_id,
                                     uint8_t *scratch_buf, uint16_t scratch_len) {
    if (LoRa_RingBuffer_IsEmpty(&s_RxRing)) return false;
    
    uint16_t count = LoRa_RingBuffer_GetCount(&s_RxRing);
    if (count > scratch_len) count = scratch_len; // 保护防止溢出
    
    // 1. 偷看所有数据 (Peek) 到共享缓冲区
    uint16_t chunk1 = s_RxRing.Size - s_RxRing.Tail;
    if (count <= chunk1) {
        memcpy(scratch_buf, &s_RxRing.pBuffer[s_RxRing.Tail], count);
    } else {
        memcpy(scratch_buf, &s_RxRing.pBuffer[s_RxRing.Tail], chunk1);
        memcpy(scratch_buf + chunk1, &s_RxRing.pBuffer[0], count - chunk1);
    }
    
    // 2. 尝试解析
    uint16_t consumed = LoRa_Manager_Protocol_Unpack(scratch_buf, count, packet, local_id, group_id);
    
    // 3. 如果消耗了数据，从 RingBuffer 移除
    if (consumed > 0) {
        uint8_t dummy;
        for(uint16_t i=0; i<consumed; i++) {
            LoRa_RingBuffer_Read(&s_RxRing, &dummy, 1);
        }
        
        // 只有当 packet 被有效填充时才返回 true
        // 简单的判断依据：如果是 ACK 包，或者 PayloadLen > 0
        // 注意：Protocol_Unpack 内部如果 CRC 错误会返回 consumed 但不填充 packet
        // 所以这里需要调用者保证 packet 初始为 0，并检查关键字段
        return (packet->IsAckPacket || packet->PayloadLen > 0);
    }
    
    return false;
}
