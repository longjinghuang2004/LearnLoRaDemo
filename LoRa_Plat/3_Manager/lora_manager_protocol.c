/**
  ******************************************************************************
  * @file    lora_manager_protocol.c
  * @author  LoRaPlat Team
  * @brief   LoRa 协议封包解包实现 (V3.9.5 - 16-bit Seq Support)
  ******************************************************************************
  */

#include "lora_manager_protocol.h"
#include "lora_crc16.h"
#include "lora_osal.h"
#include <string.h>

// ============================================================
//                    1. 封包实现 (Pack)
// ============================================================

uint16_t LoRa_Manager_Protocol_Pack(const LoRa_Packet_t *packet, 
                                    uint8_t *buffer, 
                                    uint16_t buffer_size,
                                    uint8_t tmode,
                                    uint8_t channel)
{
    LORA_CHECK(packet && buffer && buffer_size > 0, 0);
    
    uint16_t idx = 0;
    
    // 1. 定点模式头部 (Target Addr + Channel) - 仅用于物理层辅助，不计入协议校验
    if (tmode == 1) {
        if (idx + 3 > buffer_size) return 0;
        buffer[idx++] = (uint8_t)(packet->TargetID >> 8);   // High Byte
        buffer[idx++] = (uint8_t)(packet->TargetID & 0xFF); // Low Byte
        buffer[idx++] = channel;
    }
    
    // --- 协议帧开始 ---
    
    // 2. 协议头 (CM)
    if (idx + 2 > buffer_size) return 0;
    buffer[idx++] = LORA_PROTOCOL_HEAD_0;
    buffer[idx++] = LORA_PROTOCOL_HEAD_1;
    
    // 3. 长度 (Payload Len)
    if (idx + 1 > buffer_size) return 0;
    buffer[idx++] = packet->PayloadLen;
    
    // 4. 控制字 (Ctrl)
    uint8_t ctrl = 0;
    if (packet->IsAckPacket) ctrl |= LORA_CTRL_MASK_TYPE;
    if (packet->NeedAck)     ctrl |= LORA_CTRL_MASK_NEED_ACK;
    if (packet->HasCrc)      ctrl |= LORA_CTRL_MASK_HAS_CRC;
    
    if (idx + 1 > buffer_size) return 0;
    buffer[idx++] = ctrl;
    
    // 5. [变更] 序号 (Seq) - 升级为 16位 (2 Bytes, Little Endian)
    if (idx + 2 > buffer_size) return 0;
    buffer[idx++] = (uint8_t)(packet->Sequence & 0xFF);
    buffer[idx++] = (uint8_t)(packet->Sequence >> 8);
    
    // 6. 地址域 (TargetID + SourceID) - 4 Bytes
    if (idx + 4 > buffer_size) return 0;
    buffer[idx++] = (uint8_t)(packet->TargetID & 0xFF);
    buffer[idx++] = (uint8_t)(packet->TargetID >> 8);
    buffer[idx++] = (uint8_t)(packet->SourceID & 0xFF);
    buffer[idx++] = (uint8_t)(packet->SourceID >> 8);
    
    // 7. 负载 (Payload)
    if (packet->PayloadLen > 0) {
        if (idx + packet->PayloadLen > buffer_size) return 0;
        memcpy(&buffer[idx], packet->Payload, packet->PayloadLen);
        idx += packet->PayloadLen;
    }
    
    // 8. CRC16 (可选)
    if (packet->HasCrc) {
        // 计算范围：从协议头之后(Length)开始，到 Payload 结束
        // 协议帧起始位置：tmode==1 ? 3 : 0
        // 校验内容：Length(1) + Ctrl(1) + Seq(2) + Addr(4) + Payload(N)
        
        uint16_t frame_start = (tmode == 1) ? 3 : 0;
        uint16_t crc_calc_start = frame_start + 2; // 跳过 CM 头
        uint16_t crc_len = idx - crc_calc_start;
        
        uint16_t crc = LoRa_CRC16_Calculate(&buffer[crc_calc_start], crc_len);
        
        if (idx + 2 > buffer_size) return 0;
        buffer[idx++] = (uint8_t)(crc & 0xFF);
        buffer[idx++] = (uint8_t)(crc >> 8);
    }
    
    // 9. 包尾 (\r\n)
    if (idx + 2 > buffer_size) return 0;
    buffer[idx++] = LORA_PROTOCOL_TAIL_0;
    buffer[idx++] = LORA_PROTOCOL_TAIL_1;
    
    return idx;
}

// ============================================================
//                    2. 解包实现 (Unpack)
// ============================================================

uint16_t LoRa_Manager_Protocol_Unpack(const uint8_t *buffer, 
                                      uint16_t length, 
                                      LoRa_Packet_t *packet,
                                      uint16_t local_id,
                                      uint16_t group_id)
{
    // [变更] 最小包长：Head(2) + Len(1) + Ctrl(1) + Seq(2) + Addr(4) + Tail(2) = 12字节
    if (length < 12) return 0;
    
    // 1. 寻找包头 (CM)
    if (buffer[0] != LORA_PROTOCOL_HEAD_0 || buffer[1] != LORA_PROTOCOL_HEAD_1) {
        return 1; // 丢弃 1 字节重试
    }
    
    // 2. 解析基础字段
    uint8_t p_len = buffer[2];
    uint8_t ctrl  = buffer[3];
    bool has_crc  = (ctrl & LORA_CTRL_MASK_HAS_CRC);
    
    // 3. [变更] 计算预期总长度
    // 基础(10) + Payload(p_len) + CRC(2 if has) + Tail(2)
    // 基础包括：Head(2)+Len(1)+Ctrl(1)+Seq(2)+Addr(4) = 10
    uint16_t expected_len = 10 + p_len + (has_crc ? 2 : 0) + 2;
    
    // 4. 长度检查
    if (expected_len > length) {
        return 0; // 数据不够，继续等待
    }
    
    // 5. 包尾检查
    if (buffer[expected_len - 2] != LORA_PROTOCOL_TAIL_0 || 
        buffer[expected_len - 1] != LORA_PROTOCOL_TAIL_1) {
        return 1; // 包尾错误，丢弃包头
    }
    
    // 6. CRC 校验
    if (has_crc) {
        // 校验范围：从 Len(buffer[2]) 开始，到 Payload 结束
        // 长度 = expected_len - Head(2) - CRC(2) - Tail(2) = expected_len - 6
        uint16_t calc_len = expected_len - 6;
        uint16_t calc_crc = LoRa_CRC16_Calculate(&buffer[2], calc_len);
        
        uint16_t recv_crc = (uint16_t)buffer[expected_len - 4] | 
                            ((uint16_t)buffer[expected_len - 3] << 8);
                            
        if (calc_crc != recv_crc) {
            return expected_len; // CRC 失败，丢弃整包
        }
    }
    
    // 7. [变更] 地址过滤 (偏移量 +1)
    // TargetID 在 buffer[6], buffer[7]
    uint16_t target = (uint16_t)buffer[6] | ((uint16_t)buffer[7] << 8);
    
    bool accept = (target == local_id) || 
                  (target == 0xFFFF) || 
                  (group_id != 0 && target == group_id);
                  
    if (!accept) {
        return expected_len; // 不是发给我的，丢弃
    }
    
    // 8. [变更] 填充输出结构体 (偏移量 +1)
    if (packet) {
        packet->IsAckPacket = (ctrl & LORA_CTRL_MASK_TYPE);
        packet->NeedAck     = (ctrl & LORA_CTRL_MASK_NEED_ACK);
        packet->HasCrc      = has_crc;
        
        // Seq: buffer[4], buffer[5]
        packet->Sequence    = (uint16_t)buffer[4] | ((uint16_t)buffer[5] << 8);
        
        packet->TargetID    = target;
        
        // SourceID: buffer[8], buffer[9]
        packet->SourceID    = (uint16_t)buffer[8] | ((uint16_t)buffer[9] << 8);
        
        packet->PayloadLen  = p_len;
        
        // Payload: buffer[10] 开始
        if (p_len > 0 && p_len <= LORA_MAX_PAYLOAD_LEN) {
            memcpy(packet->Payload, &buffer[10], p_len);
        }
    }
    
    return expected_len;
}
