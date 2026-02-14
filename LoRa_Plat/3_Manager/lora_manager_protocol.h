/**
  ******************************************************************************
  * @file    lora_manager_protocol.h
  * @author  LoRaPlat Team
  * @brief   LoRa 协议定义与封包解包接口
  ******************************************************************************
  */

#ifndef __LORA_MANAGER_PROTOCOL_H
#define __LORA_MANAGER_PROTOCOL_H

#include <stdint.h>
#include <stdbool.h>
#include "LoRaPlatConfig.h"

// ============================================================
//                    1. 协议常量定义
// ============================================================
#define LORA_PROTOCOL_HEAD_0     'C'
#define LORA_PROTOCOL_HEAD_1     'M'
#define LORA_PROTOCOL_TAIL_0     '\r'
#define LORA_PROTOCOL_TAIL_1     '\n'

#define LORA_CTRL_MASK_TYPE      0x80 // 1=ACK, 0=Data
#define LORA_CTRL_MASK_NEED_ACK  0x40 // 1=Need ACK
#define LORA_CTRL_MASK_HAS_CRC   0x20 // 1=Has CRC

// 最大负载长度 (根据缓冲区大小估算，预留头部开销)
#define LORA_MAX_PAYLOAD_LEN     200

// ============================================================
//                    2. 数据包结构体
// ============================================================

/**
 * @brief LoRa 逻辑数据包结构
 * @note  这是解包后的结构化数据，不包含 Head/Tail 等物理层字节
 */
typedef struct {
    // --- 控制域 ---
    bool     IsAckPacket;    // 是否为 ACK 包
    bool     NeedAck;        // 是否需要回复 ACK
    bool     HasCrc;         // 是否包含 CRC
    
    // --- 地址域 ---
    uint16_t TargetID;       // 目标 ID
    uint16_t SourceID;       // 源 ID (发送方)
    
    // --- 序号与负载 ---
    uint8_t  Sequence;       // 包序号
    uint8_t  PayloadLen;     // 负载长度
    uint8_t  Payload[LORA_MAX_PAYLOAD_LEN]; // 负载数据
    
} LoRa_Packet_t;

// ============================================================
//                    3. 核心接口
// ============================================================

/**
 * @brief  将结构体打包为字节流 (Serialize)
 * @param  packet: 待发送的数据包结构体
 * @param  buffer: 输出缓冲区
 * @param  buffer_size: 缓冲区最大大小
 * @param  tmode: 当前传输模式 (0=透传, 1=定点) - 影响包头格式
 * @param  channel: 当前信道 (仅定点模式需要)
 * @return 打包后的字节总长度 (0表示失败)
 */
uint16_t LoRa_Manager_Protocol_Pack(const LoRa_Packet_t *packet, 
                                    uint8_t *buffer, 
                                    uint16_t buffer_size,
                                    uint8_t tmode,
                                    uint8_t channel);

/**
 * @brief  尝试从缓冲区解析一个完整数据包 (Deserialize)
 * @param  buffer: 输入数据缓冲区
 * @param  length: 缓冲区有效数据长度
 * @param  packet: 输出解析后的结构体
 * @param  local_id: 本地 ID (用于地址过滤)
 * @param  group_id: 组 ID (用于地址过滤)
 * @return 解析消耗的字节数 (0表示未找到完整包，>0表示成功解析并消耗了多少字节)
 */
uint16_t LoRa_Manager_Protocol_Unpack(const uint8_t *buffer, 
                                      uint16_t length, 
                                      LoRa_Packet_t *packet,
                                      uint16_t local_id,
                                      uint16_t group_id);

#endif // __LORA_MANAGER_PROTOCOL_H
