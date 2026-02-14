#include "lora_manager_buffer.h"
#include "lora_ring_buffer.h"
#include "lora_port.h"
#include "LoRaPlatConfig.h"
#include "lora_osal.h" // 引入临界区支持
#include <string.h>

// 缓冲区大小定义
#define TX_QUEUE_SIZE   MGR_TX_BUF_SIZE
#define RX_QUEUE_SIZE   MGR_RX_BUF_SIZE
#define ACK_QUEUE_SIZE  64 // ACK 队列较小，足够存 2-3 个 ACK 包

// 静态缓冲区
static uint8_t s_TxBufArr[TX_QUEUE_SIZE];
static uint8_t s_RxBufArr[RX_QUEUE_SIZE];
static uint8_t s_AckBufArr[ACK_QUEUE_SIZE]; // [新增] ACK 专用缓冲区

// 环形队列句柄
static LoRa_RingBuffer_t s_TxRing;
static LoRa_RingBuffer_t s_RxRing;
static LoRa_RingBuffer_t s_AckRing; // [新增] ACK 专用队列

void LoRa_Manager_Buffer_Init(void) {
    LoRa_RingBuffer_Init(&s_TxRing, s_TxBufArr, TX_QUEUE_SIZE);
    LoRa_RingBuffer_Init(&s_RxRing, s_RxBufArr, RX_QUEUE_SIZE);
    LoRa_RingBuffer_Init(&s_AckRing, s_AckBufArr, ACK_QUEUE_SIZE);
}

// ============================================================
//                    普通发送队列 (Tx Queue)
// ============================================================

bool LoRa_Manager_Buffer_PushTx(const LoRa_Packet_t *packet, uint8_t tmode, uint8_t channel, 
                                uint8_t *scratch_buf, uint16_t scratch_len) {
    if (!scratch_buf) return false;

    // 1. 序列化 (耗时操作，在临界区外进行，使用传入的栈内存)
    uint16_t len = LoRa_Manager_Protocol_Pack(packet, scratch_buf, scratch_len, tmode, channel);
    if (len == 0) return false;
    
    // 2. 入队 (快速操作，必须原子化)
    bool ret = false;
    
    uint32_t primask = OSAL_EnterCritical(); // 【关中断/加锁】
    
    if (LoRa_RingBuffer_GetFree(&s_TxRing) >= len) {
        LoRa_RingBuffer_Write(&s_TxRing, scratch_buf, len);
        ret = true;
    }
    
    OSAL_ExitCritical(primask);  // 【开中断/解锁】
    
    return ret;
}

bool LoRa_Manager_Buffer_HasTxData(void) {
    return !LoRa_RingBuffer_IsEmpty(&s_TxRing);
}

uint16_t LoRa_Manager_Buffer_PeekTx(uint8_t *scratch_buf, uint16_t scratch_len) {
    // Peek 操作只读不写，且是在单线程 Run 中调用，通常不需要加锁
    // 但为了绝对安全（防止 Push 修改 Head 导致 Count 变化），建议加锁读取 Count
    // 这里简化处理：假设 Run 是单线程的，且 Push 只修改 Head，Peek 只读 Tail 和 Count
    // 只要 Count 读取是原子的，Peek 就是安全的。
    
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
    
    uint32_t primask = OSAL_EnterCritical(); // 【关中断/加锁】
    
    // 简单粗暴的移动指针方式
    for(uint16_t i=0; i<len; i++) {
        LoRa_RingBuffer_Read(&s_TxRing, &dummy, 1);
    }
    
    OSAL_ExitCritical(primask);  // 【开中断/解锁】
}

// ============================================================
//                    ACK 高优先级队列 (Ack Queue)
// ============================================================

bool LoRa_Manager_Buffer_PushAck(const LoRa_Packet_t *packet, uint8_t tmode, uint8_t channel, 
                                 uint8_t *scratch_buf, uint16_t scratch_len) {
    if (!scratch_buf) return false;

    // 1. 序列化
    uint16_t len = LoRa_Manager_Protocol_Pack(packet, scratch_buf, scratch_len, tmode, channel);
    if (len == 0) return false;
    
    // 2. 入队 (原子操作)
    bool ret = false;
    
    uint32_t primask = OSAL_EnterCritical();
    
    if (LoRa_RingBuffer_GetFree(&s_AckRing) >= len) {
        LoRa_RingBuffer_Write(&s_AckRing, scratch_buf, len);
        ret = true;
    }
    
    OSAL_ExitCritical(primask);
    
    return ret;
}

bool LoRa_Manager_Buffer_HasAckData(void) {
    return !LoRa_RingBuffer_IsEmpty(&s_AckRing);
}

uint16_t LoRa_Manager_Buffer_PeekAck(uint8_t *scratch_buf, uint16_t scratch_len) {
    if (s_AckRing.Count == 0) return 0;
    
    uint16_t len = s_AckRing.Count;
    if (len > scratch_len) len = scratch_len;
    
    uint16_t chunk1 = s_AckRing.Size - s_AckRing.Tail;
    if (len <= chunk1) {
        memcpy(scratch_buf, &s_AckRing.pBuffer[s_AckRing.Tail], len);
    } else {
        memcpy(scratch_buf, &s_AckRing.pBuffer[s_AckRing.Tail], chunk1);
        memcpy(scratch_buf + chunk1, &s_AckRing.pBuffer[0], len - chunk1);
    }
    return len;
}

void LoRa_Manager_Buffer_PopAck(uint16_t len) {
    uint8_t dummy;
    
    uint32_t primask = OSAL_EnterCritical();
    
    for(uint16_t i=0; i<len; i++) {
        LoRa_RingBuffer_Read(&s_AckRing, &dummy, 1);
    }
    
    OSAL_ExitCritical(primask);
}

// ============================================================
//                    接收处理 (RX Buffer)
// ============================================================

uint16_t LoRa_Manager_Buffer_PullFromPort(void) {
    // 这里可以使用小一点的栈变量，因为是从 DMA 读，通常不会太大
    uint8_t temp_buf[64]; 
    uint16_t total_read = 0;
    
    while (1) {
        uint16_t len = LoRa_Port_ReceiveData(temp_buf, sizeof(temp_buf));
        if (len == 0) break;
        
        // [新增] 打印接收到的原始数据
        LORA_HEXDUMP("RX RAW", temp_buf, len);
        
        // RX 仅由单线程 Run 调用，不需要加锁
        LoRa_RingBuffer_Write(&s_RxRing, temp_buf, len);
        total_read += len;
    }
    return total_read;
}

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
        return (packet->IsAckPacket || packet->PayloadLen > 0);
    }
    
    return false;
}
