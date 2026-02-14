/**
  ******************************************************************************
  * @file    lora_manager_buffer.h
  * @author  LoRaPlat Team
  * @brief   LoRa 收发缓冲区管理接口 (双队列策略 + 线程安全)
  ******************************************************************************
  */

#ifndef __LORA_MANAGER_BUFFER_H
#define __LORA_MANAGER_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include "lora_manager_protocol.h"

// ============================================================
//                    1. 初始化
// ============================================================

/**
 * @brief  初始化收发缓冲区 (Tx Ring, Ack Ring, Rx Ring)
 */
void LoRa_Manager_Buffer_Init(void);

// ============================================================
//                    2. 普通发送队列 (Tx Queue)
// ============================================================

/**
 * @brief  将普通数据包推入发送队列 (线程安全)
 * @param  packet: 待发送的数据包结构体
 * @param  tmode: 传输模式
 * @param  channel: 信道
 * @param  scratch_buf: 外部传入的栈缓冲区 (用于序列化)
 * @param  scratch_len: 缓冲区长度
 * @return true=成功入队, false=队列满
 */
bool LoRa_Manager_Buffer_PushTx(const LoRa_Packet_t *packet, uint8_t tmode, uint8_t channel, 
                                uint8_t *scratch_buf, uint16_t scratch_len);

/**
 * @brief  检查普通发送队列是否有数据
 */
bool LoRa_Manager_Buffer_HasTxData(void);

/**
 * @brief  从普通发送队列头部预览数据 (Peek)
 * @param  scratch_buf: 输出缓冲区
 * @param  scratch_len: 缓冲区大小
 * @return 数据长度
 */
uint16_t LoRa_Manager_Buffer_PeekTx(uint8_t *scratch_buf, uint16_t scratch_len);

/**
 * @brief  从普通发送队列移除已发送的数据 (Pop) (线程安全)
 * @param  len: 要移除的长度
 */
void LoRa_Manager_Buffer_PopTx(uint16_t len);

// ============================================================
//                    3. ACK 高优先级队列 (Ack Queue)
// ============================================================

/**
 * @brief  将 ACK 包推入高优先级队列 (线程安全)
 * @note   ACK 包通常很小 (<20字节)，且必须优先发送
 */
bool LoRa_Manager_Buffer_PushAck(const LoRa_Packet_t *packet, uint8_t tmode, uint8_t channel, 
                                 uint8_t *scratch_buf, uint16_t scratch_len);

/**
 * @brief  检查 ACK 队列是否有数据
 */
bool LoRa_Manager_Buffer_HasAckData(void);

/**
 * @brief  从 ACK 队列头部预览数据
 */
uint16_t LoRa_Manager_Buffer_PeekAck(uint8_t *scratch_buf, uint16_t scratch_len);

/**
 * @brief  从 ACK 队列移除数据 (线程安全)
 */
void LoRa_Manager_Buffer_PopAck(uint16_t len);

// ============================================================
//                    4. 接收处理 (RX Buffer)
// ============================================================

/**
 * @brief  从 Port 层拉取数据并推入 RX RingBuffer
 * @return 拉取到的字节数
 */
uint16_t LoRa_Manager_Buffer_PullFromPort(void);

/**
 * @brief  尝试从 RX RingBuffer 解析一个完整包
 * @param  packet: 输出结构体
 * @param  local_id: 本地 ID
 * @param  group_id: 组 ID
 * @param  scratch_buf: 外部传入的共享缓冲区 (RX Workspace)
 * @param  scratch_len: 缓冲区长度
 * @return true=解析成功, false=无完整包
 */
bool LoRa_Manager_Buffer_GetRxPacket(LoRa_Packet_t *packet, uint16_t local_id, uint16_t group_id,
                                     uint8_t *scratch_buf, uint16_t scratch_len);

#endif // __LORA_MANAGER_BUFFER_H
