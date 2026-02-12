#include "lora_manager.h"
#include "lora_driver.h"
#include "lora_port.h"
#include <string.h>
#include <stdio.h> // 用于调试打印

// ============================================================
//                    1. 内部变量与定义
// ============================================================

// 协议头内容 (固定部分)
#define PROTOCOL_HEAD_SIZE    2  // 'C', 'M'
#define PROTOCOL_HEAD_0       'C'
#define PROTOCOL_HEAD_1       'M'
#define PROTOCOL_TAIL_0       '\r'
#define PROTOCOL_TAIL_1       '\n'

// 控制字掩码
#define CTRL_MASK_TYPE        0x80 
#define CTRL_MASK_NEED_ACK    0x40 
#define CTRL_MASK_HAS_CRC     0x20 

// Manager 全局实例
LoRa_Manager_t g_LoRaManager;

// 内部发送长度缓存 (用于重传)
static uint16_t s_CurrentTxLen = 0;

// ============================================================
//                    2. 内部函数声明
// ============================================================
static uint16_t CRC16_Calc(const uint8_t *data, uint16_t len);
static void Manager_ProcessRx(void);
static void Manager_SendACK(uint16_t target_id, uint8_t seq);
static void Manager_ResetState(void);

// ============================================================
//                    3. 驱动层回调适配
// ============================================================

/**
 * @brief 接收驱动层异步事件的回调
 * @param result: LORA_OK 或 错误码
 */
static void _OnDriverEvent(LoRa_Result_t result) {
    if (result == LORA_OK) {
        // --- 发送成功 ---
        if (g_LoRaManager.state == MGR_STATE_TX_SENDING) {
            // 检查是否需要等待 ACK
            // 注意：这里简化处理，直接从 TxBuffer 读取控制字判断
            // 实际应根据 TMODE 偏移量读取
            uint8_t ctrl_offset = (g_LoRaConfig_Current.tmode == LORA_TMODE_FIXED) ? 6 : 3;
            
            if (g_LoRaManager.TxBuffer[ctrl_offset] & CTRL_MASK_NEED_ACK) {
                g_LoRaManager.state = MGR_STATE_WAIT_ACK;
                g_LoRaManager.state_tick = Port_GetTick();
                LORA_LOG("[MGR] TX Done. Waiting ACK...");
            } else {
                // 不需要 ACK，流程结束
                LORA_LOG("[MGR] TX Done. No ACK needed.");
                if (g_LoRaManager.cb_on_tx) g_LoRaManager.cb_on_tx(true);
                Manager_ResetState();
            }
        }
    } 
    else {
        // --- 发送失败 (超时/硬件错误) ---
        LORA_LOG("[MGR] Driver Error: %d", result);
        if (g_LoRaManager.cb_on_err) g_LoRaManager.cb_on_err(result);
        
        // 驱动层已经自动复位了，Manager 层只需重置逻辑状态
        Manager_ResetState();
        if (g_LoRaManager.cb_on_tx) g_LoRaManager.cb_on_tx(false);
    }
}

// ============================================================
//                    4. 接口实现
// ============================================================

void Manager_Init(OnRxData_t on_rx, OnTxResult_t on_tx, OnError_t on_err)
{
    // 1. 初始化驱动层 (注入回调)
    Drv_Init(_OnDriverEvent);
    
    // 2. 清空缓冲区
    Port_ClearRxBuffer();
    
    // 3. 初始化 Manager 状态
    g_LoRaManager.cb_on_rx = on_rx;
    g_LoRaManager.cb_on_tx = on_tx;
    g_LoRaManager.cb_on_err = on_err;
    
    Manager_ResetState();
    g_LoRaManager.tx_seq = 0;
    g_LoRaManager.rx_len = 0; 
    
    LORA_LOG("[MGR] Init Done. NetID:0x%04X", g_LoRaManager.local_id);
}

bool Manager_SendPacket(const uint8_t *payload, uint16_t len, uint16_t target_id)
{
    // 1. 状态检查
    if (g_LoRaManager.state != MGR_STATE_IDLE) {
        LORA_LOG("[MGR] Busy! State: %d", g_LoRaManager.state);
        return false;
    }
    
    // 2. 组包 (保持原有逻辑)
    uint16_t overhead = 11 + (LORA_ENABLE_CRC ? 2 : 0);
    if (g_LoRaConfig_Current.tmode == LORA_TMODE_FIXED) {
        overhead += 3;
    }

    if (len + overhead > MGR_TX_BUF_SIZE) {
        if (g_LoRaManager.cb_on_err) g_LoRaManager.cb_on_err(LORA_ERR_MEM_OVERFLOW);
        return false;
    }
    
    uint8_t *p = g_LoRaManager.TxBuffer;
    uint16_t idx = 0;
    
    // 插入定向头
    if (g_LoRaConfig_Current.tmode == LORA_TMODE_FIXED) {
        p[idx++] = (uint8_t)(target_id >> 8);
        p[idx++] = (uint8_t)(target_id & 0xFF);
        p[idx++] = g_LoRaConfig_Current.channel;
    }
    
    // 协议头
    p[idx++] = PROTOCOL_HEAD_0;
    p[idx++] = PROTOCOL_HEAD_1;
    p[idx++] = (uint8_t)len;
    
    uint8_t ctrl = 0;
    if (LORA_ENABLE_ACK) ctrl |= CTRL_MASK_NEED_ACK;
    if (LORA_ENABLE_CRC) ctrl |= CTRL_MASK_HAS_CRC;
    p[idx++] = ctrl;
    
    p[idx++] = ++g_LoRaManager.tx_seq;
    
    p[idx++] = (uint8_t)(target_id & 0xFF);
    p[idx++] = (uint8_t)(target_id >> 8);
    p[idx++] = (uint8_t)(g_LoRaManager.local_id & 0xFF);
    p[idx++] = (uint8_t)(g_LoRaManager.local_id >> 8);
    
    memcpy(&p[idx], payload, len);
    idx += len;
    
    if (LORA_ENABLE_CRC) {
        uint16_t crc_start_idx = (g_LoRaConfig_Current.tmode == LORA_TMODE_FIXED) ? 3 : 0;
        uint16_t crc = CRC16_Calc(&p[crc_start_idx + 2], idx - crc_start_idx - 2);
        p[idx++] = (uint8_t)(crc & 0xFF);
        p[idx++] = (uint8_t)(crc >> 8);
    }
    
    p[idx++] = PROTOCOL_TAIL_0;
    p[idx++] = PROTOCOL_TAIL_1;
    
    s_CurrentTxLen = idx;
    
    // 3. 调用驱动层异步发送
    LoRa_Result_t res = Drv_AsyncSend(g_LoRaManager.TxBuffer, s_CurrentTxLen);
    
    if (res == LORA_OK) {
        g_LoRaManager.state = MGR_STATE_TX_SENDING;
        g_LoRaManager.state_tick = Port_GetTick();
        g_LoRaManager.retry_cnt = 0;
        return true;
    } else {
        LORA_LOG("[MGR] Drv Reject: %d", res);
        return false;
    }
}

void Manager_Run(void)
{
    // 1. 驱动层心跳 (必须调用!)
    Drv_Run();
    
    uint32_t now = Port_GetTick();
    
    // 2. RX 处理 (从 DMA 读取数据)
    // 注意：Port_ReadData 是非阻塞的
    uint16_t read_len = Port_ReadData(
        &g_LoRaManager.RxBuffer[g_LoRaManager.rx_len], 
        MGR_RX_BUF_SIZE - g_LoRaManager.rx_len
    );
    
    if (read_len > 0) {
        g_LoRaManager.rx_len += read_len;
        Manager_ProcessRx();
    }
    
    // 3. 业务层超时处理 (ACK 超时)
    switch (g_LoRaManager.state)
    {
        case MGR_STATE_IDLE: 
        case MGR_STATE_TX_SENDING: 
            // 发送超时由 Driver 层处理，这里不用管
            break;
            
        case MGR_STATE_WAIT_ACK:
            if (now - g_LoRaManager.state_tick > LORA_ACK_TIMEOUT_MS) {
                if (g_LoRaManager.retry_cnt < LORA_MAX_RETRY) {
                    g_LoRaManager.retry_cnt++;
                    LORA_LOG("[MGR] ACK Timeout. Retry %d", g_LoRaManager.retry_cnt);
                    
                    // 重试发送
                    if (Drv_AsyncSend(g_LoRaManager.TxBuffer, s_CurrentTxLen) == LORA_OK) {
                        g_LoRaManager.state = MGR_STATE_TX_SENDING;
                        g_LoRaManager.state_tick = now;
                    } else {
                        // 驱动忙，稍后重试? 这里简单处理为失败
                        Manager_ResetState();
                        if (g_LoRaManager.cb_on_err) g_LoRaManager.cb_on_err(LORA_ERR_BUSY);
                    }
                } else {
                    LORA_LOG("[MGR] Err: ACK Failed");
                    if (g_LoRaManager.cb_on_err) g_LoRaManager.cb_on_err(LORA_ERR_ACK_TIMEOUT);
                    if (g_LoRaManager.cb_on_tx) g_LoRaManager.cb_on_tx(false);
                    Manager_ResetState();
                }
            }
            break;
            
        default: break;
    }
}

// ============================================================
//                    5. 内部逻辑 (CRC, RX, ACK)
// ============================================================

static void Manager_ResetState(void) {
    g_LoRaManager.state = MGR_STATE_IDLE;
}

static uint16_t CRC16_Calc(const uint8_t *data, uint16_t len) {
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

static void Manager_ProcessRx(void) {
    uint16_t i = 0;
    uint16_t processed_len = 0;
    
    while (i + 11 <= g_LoRaManager.rx_len) {
        // 寻找包头 CM
        if (g_LoRaManager.RxBuffer[i] == PROTOCOL_HEAD_0 && 
            g_LoRaManager.RxBuffer[i+1] == PROTOCOL_HEAD_1) {
            
            uint8_t payload_len = g_LoRaManager.RxBuffer[i+2];
            uint8_t ctrl = g_LoRaManager.RxBuffer[i+3];
            bool has_crc = (ctrl & CTRL_MASK_HAS_CRC) ? true : false;
            
            uint16_t packet_len = 2 + 1 + 1 + 1 + 2 + 2 + payload_len + (has_crc ? 2 : 0) + 2;
            
            if (i + packet_len > g_LoRaManager.rx_len) break; // 数据未收全
            
            // 验证包尾
            if (g_LoRaManager.RxBuffer[i + packet_len - 2] == PROTOCOL_TAIL_0 && 
                g_LoRaManager.RxBuffer[i + packet_len - 1] == PROTOCOL_TAIL_1) {
                
                // 提取信息
                uint16_t target_id = (uint16_t)g_LoRaManager.RxBuffer[i+5] | ((uint16_t)g_LoRaManager.RxBuffer[i+6] << 8);
                uint16_t src_id    = (uint16_t)g_LoRaManager.RxBuffer[i+7] | ((uint16_t)g_LoRaManager.RxBuffer[i+8] << 8);
                uint8_t  seq       = g_LoRaManager.RxBuffer[i+4];
                
                // 地址过滤
                bool accept = false;
                if (target_id == g_LoRaManager.local_id) accept = true;
                else if (target_id == LORA_ID_BROADCAST) accept = true;
                else if (g_LoRaManager.group_id != 0 && target_id == g_LoRaManager.group_id) accept = true;
                else if (g_LoRaManager.local_id == LORA_ID_UNASSIGNED && target_id == LORA_ID_UNASSIGNED) accept = true;

                if (accept) {
                    bool crc_ok = true;
                    if (has_crc) {
                        uint16_t calc_len = 1 + 1 + 1 + 4 + payload_len;
                        uint16_t calc_crc = CRC16_Calc(&g_LoRaManager.RxBuffer[i+2], calc_len);
                        uint16_t recv_crc = (uint16_t)g_LoRaManager.RxBuffer[i + packet_len - 4] | 
                                            ((uint16_t)g_LoRaManager.RxBuffer[i + packet_len - 3] << 8);
                        if (calc_crc != recv_crc) crc_ok = false;
                    }
                    
                    if (crc_ok) {
                        if ((ctrl & CTRL_MASK_TYPE) != 0) { // ACK 包
                            if (g_LoRaManager.state == MGR_STATE_WAIT_ACK) {
                                LORA_LOG("[MGR] ACK Recv from 0x%04X", src_id);
                                if (g_LoRaManager.cb_on_tx) g_LoRaManager.cb_on_tx(true);
                                Manager_ResetState();
                            }
                        } else { // 数据包
                            // [修改点 1] 优先发送 ACK (解决 ACK 丢失问题)
                            // 必须在回调之前发，否则应用层如果立即回包，会抢占驱动导致 ACK 发送失败
                            if ((ctrl & CTRL_MASK_NEED_ACK) && target_id != LORA_ID_BROADCAST) {
                                Manager_SendACK(src_id, seq);
                            }

                            // [修改点 2] 触发应用层回调
                            uint8_t *p_payload = &g_LoRaManager.RxBuffer[i + 9];
                            if (g_LoRaManager.cb_on_rx) {
                                g_LoRaManager.cb_on_rx(p_payload, payload_len, src_id);
                            }
                        }
                    } else {
                        LORA_LOG("[MGR] CRC Fail");
                    }
                }
                i += packet_len;
                processed_len = i;
            } else {
                i++;
                processed_len = i;
            }
        } else {
            i++;
            processed_len = i;
        }
    }
    
    if (processed_len > 0) {
        if (processed_len < g_LoRaManager.rx_len) {
            memmove(g_LoRaManager.RxBuffer, &g_LoRaManager.RxBuffer[processed_len], g_LoRaManager.rx_len - processed_len);
            g_LoRaManager.rx_len -= processed_len;
        } else {
            g_LoRaManager.rx_len = 0;
        }
    }
}

static void Manager_SendACK(uint16_t target_id, uint8_t seq) {
    // 注意：ACK 发送也必须是异步的
    // 但由于 ACK 通常很短，且不需要重传，我们这里直接尝试发送
    // 如果驱动忙，ACK 可能会丢失，这是允许的
    
    uint8_t ack_buf[32];
    uint16_t idx = 0;
    
    if (g_LoRaConfig_Current.tmode == LORA_TMODE_FIXED) {
        ack_buf[idx++] = (uint8_t)(target_id >> 8);
        ack_buf[idx++] = (uint8_t)(target_id & 0xFF);
        ack_buf[idx++] = g_LoRaConfig_Current.channel;
    }

    ack_buf[idx++] = PROTOCOL_HEAD_0;
    ack_buf[idx++] = PROTOCOL_HEAD_1;
    ack_buf[idx++] = 0; 
    
    uint8_t ctrl = CTRL_MASK_TYPE; 
    if (LORA_ENABLE_CRC) ctrl |= CTRL_MASK_HAS_CRC;
    ack_buf[idx++] = ctrl;
    
    ack_buf[idx++] = seq; 
    
    ack_buf[idx++] = (uint8_t)(target_id & 0xFF);
    ack_buf[idx++] = (uint8_t)(target_id >> 8);
    ack_buf[idx++] = (uint8_t)(g_LoRaManager.local_id & 0xFF);
    ack_buf[idx++] = (uint8_t)(g_LoRaManager.local_id >> 8);
    
    if (LORA_ENABLE_CRC) {
        uint16_t crc_start_idx = (g_LoRaConfig_Current.tmode == LORA_TMODE_FIXED) ? 3 : 0;
        uint16_t crc = CRC16_Calc(&ack_buf[crc_start_idx + 2], idx - crc_start_idx - 2);
        ack_buf[idx++] = (uint8_t)(crc & 0xFF);
        ack_buf[idx++] = (uint8_t)(crc >> 8);
    }
    
    ack_buf[idx++] = PROTOCOL_TAIL_0;
    ack_buf[idx++] = PROTOCOL_TAIL_1;
    
    // 异步发送 ACK
    Drv_AsyncSend(ack_buf, idx);
}
