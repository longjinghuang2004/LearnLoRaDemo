/**
  ******************************************************************************
  * @file    lora_manager.c
  * @author  LoRaPlat Team
  * @brief   Layer 3: 协议管理层实现
  ******************************************************************************
  */

#include "lora_manager.h"
#include "lora_port.h" // 仅用于 GetTick 和 Read
#include <string.h>

// ============================================================
//                    1. 协议常量与宏
// ============================================================

#define PROTOCOL_HEAD_0     'C'
#define PROTOCOL_HEAD_1     'M'
#define PROTOCOL_TAIL_0     '\r'
#define PROTOCOL_TAIL_1     '\n'

// 控制字掩码
#define CTRL_MASK_ACK_REQ   0x80 // 请求 ACK
#define CTRL_MASK_ACK_RESP  0x40 // 是 ACK 回复包
#define CTRL_MASK_CRC_EN    0x20 // 启用 CRC

// 全局实例
static LoRa_Manager_t s_Mgr;

// ============================================================
//                    2. 内部辅助函数
// ============================================================

// CRC16 计算 (XMODEM)
static uint16_t _CRC16(const uint8_t *data, uint16_t len) {
    uint16_t crc = 0;
    while (len--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (int i = 0; i < 8; i++) {
            if (crc & 0x8000) crc = (crc << 1) ^ 0x1021;
            else crc <<= 1;
        }
    }
    return crc;
}

// 重置状态
static void _Mgr_ResetState(void) {
    s_Mgr.state = MGR_STATE_IDLE;
}

// 构造并发送 ACK 包
static void _Mgr_SendAck(uint16_t target_id, uint8_t seq) {
    // 构造一个简短的 ACK 包
    // [Head:2][Len:1][Ctrl:1][Seq:1][Dst:2][Src:2][CRC:2][Tail:2]
    uint8_t buf[16];
    uint8_t idx = 0;
    
    buf[idx++] = PROTOCOL_HEAD_0;
    buf[idx++] = PROTOCOL_HEAD_1;
    buf[idx++] = 0; // Payload Len = 0
    buf[idx++] = CTRL_MASK_ACK_RESP | CTRL_MASK_CRC_EN;
    buf[idx++] = seq;
    buf[idx++] = (uint8_t)(target_id & 0xFF);
    buf[idx++] = (uint8_t)(target_id >> 8);
    buf[idx++] = (uint8_t)(s_Mgr.local_id & 0xFF);
    buf[idx++] = (uint8_t)(s_Mgr.local_id >> 8);
    
    uint16_t crc = _CRC16(&buf[2], idx - 2);
    buf[idx++] = (uint8_t)(crc & 0xFF);
    buf[idx++] = (uint8_t)(crc >> 8);
    
    buf[idx++] = PROTOCOL_TAIL_0;
    buf[idx++] = PROTOCOL_TAIL_1;
    
    // 直接调用 Driver 发送 (ACK 不需要重传，所以不走 Manager_Send 流程)
    Drv_AsyncSend(buf, idx);
}

// Driver 层的回调 (当物理发送完成时被调用)
static void _On_Driver_TxDone(LoRa_Result_t result) {
    if (s_Mgr.state != MGR_STATE_TX_WAIT_DRIVER) return;
    
    if (result == LORA_OK) {
        // 物理发送成功
        // 检查是否需要等待 ACK
        bool need_ack = (s_Mgr.tx_buf[3] & CTRL_MASK_ACK_REQ);
        
        if (need_ack) {
            s_Mgr.state = MGR_STATE_WAIT_ACK;
            s_Mgr.state_tick = g_LoRaPort.GetTick();
        } else {
            // 不需要 ACK，流程结束
            if (s_Mgr.cb_on_tx) s_Mgr.cb_on_tx(true);
            _Mgr_ResetState();
        }
    } else {
        // 物理发送失败 (如 AUX 超时)
        if (s_Mgr.cb_on_err) s_Mgr.cb_on_err(result);
        if (s_Mgr.cb_on_tx) s_Mgr.cb_on_tx(false);
        _Mgr_ResetState();
    }
}

// ============================================================
//                    3. 接收解析逻辑
// ============================================================

static void _Mgr_ProcessRx(void) {
    uint16_t i = 0;
    uint16_t processed_len = 0;
    
    // 简单的流式解析
    while (i + 11 <= s_Mgr.rx_len) { // 最小包长 11
        // 1. 找包头
        if (s_Mgr.rx_buf[i] == PROTOCOL_HEAD_0 && s_Mgr.rx_buf[i+1] == PROTOCOL_HEAD_1) {
            
            uint8_t p_len = s_Mgr.rx_buf[i+2];
            uint8_t ctrl  = s_Mgr.rx_buf[i+3];
            bool has_crc  = (ctrl & CTRL_MASK_CRC_EN);
            
            // 计算整包长度
            // Head(2)+Len(1)+Ctrl(1)+Seq(1)+Dst(2)+Src(2) + Payload(N) + CRC(2?) + Tail(2)
            uint16_t total_len = 9 + p_len + (has_crc ? 2 : 0) + 2;
            
            if (i + total_len > s_Mgr.rx_len) break; // 数据未收全
            
            // 2. 验证包尾
            if (s_Mgr.rx_buf[i + total_len - 2] == PROTOCOL_TAIL_0 && 
                s_Mgr.rx_buf[i + total_len - 1] == PROTOCOL_TAIL_1) {
                
                // 3. 提取信息
                uint8_t  seq    = s_Mgr.rx_buf[i+4];
                uint16_t dst_id = (uint16_t)s_Mgr.rx_buf[i+5] | (s_Mgr.rx_buf[i+6] << 8);
                uint16_t src_id = (uint16_t)s_Mgr.rx_buf[i+7] | (s_Mgr.rx_buf[i+8] << 8);
                
                // 4. 地址过滤
                bool accept = (dst_id == s_Mgr.local_id) || 
                              (dst_id == 0xFFFF) || 
                              (s_Mgr.group_id != 0 && dst_id == s_Mgr.group_id);
                
                if (accept) {
                    // 5. CRC 校验
                    bool crc_ok = true;
                    if (has_crc) {
                        uint16_t calc_crc = _CRC16(&s_Mgr.rx_buf[i+2], total_len - 6); // 从 Len 到 Payload
                        uint16_t recv_crc = (uint16_t)s_Mgr.rx_buf[i + total_len - 4] | 
                                            (s_Mgr.rx_buf[i + total_len - 3] << 8);
                        if (calc_crc != recv_crc) crc_ok = false;
                    }
                    
                    if (crc_ok) {
                        // 6. 处理包
                        if (ctrl & CTRL_MASK_ACK_RESP) {
                            // 是 ACK 包
                            if (s_Mgr.state == MGR_STATE_WAIT_ACK && dst_id == s_Mgr.local_id) {
                                // 收到期待的 ACK (这里简化处理，未校验 Seq)
                                if (s_Mgr.cb_on_tx) s_Mgr.cb_on_tx(true);
                                _Mgr_ResetState();
                            }
                        } else {
                            // 是数据包
                            if (s_Mgr.cb_on_recv) {
                                s_Mgr.cb_on_recv(&s_Mgr.rx_buf[i+9], p_len, src_id, 0);
                            }
                            // 回复 ACK (仅单播需要)
                            if ((ctrl & CTRL_MASK_ACK_REQ) && dst_id == s_Mgr.local_id) {
                                _Mgr_SendAck(src_id, seq);
                            }
                        }
                    } else {
                        if (s_Mgr.cb_on_err) s_Mgr.cb_on_err(LORA_ERR_HARDWARE); // 借用错误码表示CRC错
                    }
                }
                i += total_len;
                processed_len = i;
            } else {
                i++; // 包尾不对，滑动窗口
                processed_len = i;
            }
        } else {
            i++; // 包头不对，滑动窗口
            processed_len = i;
        }
    }
    
    // 移除已处理数据
    if (processed_len > 0) {
        if (processed_len < s_Mgr.rx_len) {
            memmove(s_Mgr.rx_buf, &s_Mgr.rx_buf[processed_len], s_Mgr.rx_len - processed_len);
            s_Mgr.rx_len -= processed_len;
        } else {
            s_Mgr.rx_len = 0;
        }
    }
}

// ============================================================
//                    4. 核心接口实现
// ============================================================

void Manager_Init(uint16_t local_id, uint16_t group_id,
                  Mgr_OnRecv_t on_recv, Mgr_OnTxResult_t on_tx, Mgr_OnError_t on_err) {
    s_Mgr.local_id = local_id;
    s_Mgr.group_id = group_id;
    s_Mgr.cb_on_recv = on_recv;
    s_Mgr.cb_on_tx = on_tx;
    s_Mgr.cb_on_err = on_err;
    
    _Mgr_ResetState();
    
    // 初始化 Driver，并注册回调
    Drv_Init(_On_Driver_TxDone);
}

LoRa_Result_t Manager_Send(const uint8_t *payload, uint16_t len, uint16_t target_id, bool need_ack) {
    if (s_Mgr.state != MGR_STATE_IDLE) return LORA_ERR_BUSY;
    if (len + 13 > MGR_TX_BUF_SIZE) return LORA_ERR_PARAM; // 13 = Overhead
    
    // 1. 组包
    uint8_t *p = s_Mgr.tx_buf;
    uint8_t idx = 0;
    
    p[idx++] = PROTOCOL_HEAD_0;
    p[idx++] = PROTOCOL_HEAD_1;
    p[idx++] = (uint8_t)len;
    
    uint8_t ctrl = CTRL_MASK_CRC_EN;
    if (need_ack) ctrl |= CTRL_MASK_ACK_REQ;
    p[idx++] = ctrl;
    
    p[idx++] = ++s_Mgr.tx_seq;
    
    p[idx++] = (uint8_t)(target_id & 0xFF);
    p[idx++] = (uint8_t)(target_id >> 8);
    p[idx++] = (uint8_t)(s_Mgr.local_id & 0xFF);
    p[idx++] = (uint8_t)(s_Mgr.local_id >> 8);
    
    memcpy(&p[idx], payload, len);
    idx += len;
    
    uint16_t crc = _CRC16(&p[2], idx - 2);
    p[idx++] = (uint8_t)(crc & 0xFF);
    p[idx++] = (uint8_t)(crc >> 8);
    
    p[idx++] = PROTOCOL_TAIL_0;
    p[idx++] = PROTOCOL_TAIL_1;
    
    s_Mgr.tx_len = idx;
    s_Mgr.retry_cnt = 0;
    
    // 2. 提交给 Driver
    LoRa_Result_t res = Drv_AsyncSend(s_Mgr.tx_buf, s_Mgr.tx_len);
    if (res == LORA_OK) {
        s_Mgr.state = MGR_STATE_TX_WAIT_DRIVER;
    }
    
    return res;
}

void Manager_Run(void) {
    // 1. 驱动心跳
    Drv_Run();
    
    // 2. 接收处理 (从 Port 读取数据)
    // 注意：这里为了保持分层，本应调用 Drv_Read，但为了效率直接读 Port 也是允许的
    // 只要 Driver 层不持有 Rx 状态即可。
    uint16_t read = g_LoRaPort.Phy_Read(&s_Mgr.rx_buf[s_Mgr.rx_len], MGR_RX_BUF_SIZE - s_Mgr.rx_len);
    if (read > 0) {
        s_Mgr.rx_len += read;
        _Mgr_ProcessRx();
    }
    
    // 3. 状态机超时处理
    if (s_Mgr.state == MGR_STATE_WAIT_ACK) {
        if (g_LoRaPort.GetTick() - s_Mgr.state_tick > MGR_ACK_TIMEOUT_MS) {
            // ACK 超时
            if (s_Mgr.retry_cnt < MGR_MAX_RETRY) {
                s_Mgr.retry_cnt++;
                // 触发重传
                if (Drv_AsyncSend(s_Mgr.tx_buf, s_Mgr.tx_len) == LORA_OK) {
                    s_Mgr.state = MGR_STATE_TX_WAIT_DRIVER;
                } else {
                    // 驱动忙 (极少见)，保持 WAIT_ACK 状态下一次循环再试? 
                    // 或者直接报错。这里选择直接报错复位。
                    if (s_Mgr.cb_on_err) s_Mgr.cb_on_err(LORA_ERR_BUSY);
                    _Mgr_ResetState();
                }
            } else {
                // 重试耗尽
                if (s_Mgr.cb_on_err) s_Mgr.cb_on_err(LORA_ERR_TIMEOUT); // ACK Timeout
                if (s_Mgr.cb_on_tx) s_Mgr.cb_on_tx(false);
                _Mgr_ResetState();
            }
        }
    }
}

bool Manager_IsIdle(void) {
    return (s_Mgr.state == MGR_STATE_IDLE) && (Drv_GetState() == DRV_STATE_IDLE);
}
