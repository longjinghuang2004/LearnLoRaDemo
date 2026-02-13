#include "lora_manager.h"
#include "lora_driver.h"
#include "lora_port.h"


//#include "Serial.h"

#include "lora_osal.h"

#include <string.h>

// 协议常量
#define PROTOCOL_HEAD_0     'C'
#define PROTOCOL_HEAD_1     'M'
#define PROTOCOL_TAIL_0     '\r'
#define PROTOCOL_TAIL_1     '\n'
#define CTRL_MASK_TYPE      0x80 
#define CTRL_MASK_NEED_ACK  0x40 
#define CTRL_MASK_HAS_CRC   0x20 

// 引入全局配置
extern LoRa_Config_t g_LoRaConfig_Current; 

LoRa_Manager_t g_LoRaManager;
static uint16_t s_CurrentTxLen = 0;

// --- 内部函数 ---
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

static void Manager_ResetState(void) {
    g_LoRaManager.state = MGR_STATE_IDLE;
    g_LoRaManager.ack_pending.pending = false;
}

// 触发物理发送
static bool _PhySend(uint8_t *data, uint16_t len) {
    if (Drv_AsyncSend(data, len)) {
        g_LoRaManager.state = MGR_STATE_TX_RUNNING;
        g_LoRaManager.state_tick = OSAL_GetTick();
        return true;
    }
    return false;
}

// 构造并发送 ACK (不立即发，只填充 buffer)
static void _PrepareAck(uint16_t target_id, uint8_t seq) {
    g_LoRaManager.ack_pending.target_id = target_id;
    g_LoRaManager.ack_pending.seq = seq;
    g_LoRaManager.ack_pending.pending = true;
    
    // 进入延时状态
    g_LoRaManager.state = MGR_STATE_ACK_DELAY;
    g_LoRaManager.state_tick = OSAL_GetTick();
}

// 实际发送 ACK
static void _SendPendingAck(void) {
    uint8_t ack_buf[32];
    uint16_t idx = 0;
    uint16_t target_id = g_LoRaManager.ack_pending.target_id;
    
    // 定向头
    if (g_LoRaConfig_Current.tmode == 1) {
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
    
    ack_buf[idx++] = g_LoRaManager.ack_pending.seq; 
    
    ack_buf[idx++] = (uint8_t)(target_id & 0xFF);
    ack_buf[idx++] = (uint8_t)(target_id >> 8);
    ack_buf[idx++] = (uint8_t)(g_LoRaManager.local_id & 0xFF);
    ack_buf[idx++] = (uint8_t)(g_LoRaManager.local_id >> 8);
    
    if (LORA_ENABLE_CRC) {
        uint16_t crc_start = (g_LoRaConfig_Current.tmode == 1) ? 3 : 0;
        uint16_t crc = CRC16_Calc(&ack_buf[crc_start + 2], idx - crc_start - 2);
        ack_buf[idx++] = (uint8_t)(crc & 0xFF);
        ack_buf[idx++] = (uint8_t)(crc >> 8);
    }
    
    ack_buf[idx++] = PROTOCOL_TAIL_0;
    ack_buf[idx++] = PROTOCOL_TAIL_1;
    
    LORA_HEXDUMP("[MGR] TX ACK", ack_buf, idx);
		
    g_LoRaManager.is_sending_ack = true; // 标记为 ACK,markB		
		
    _PhySend(ack_buf, idx);
    
    g_LoRaManager.ack_pending.pending = false;
}

static void Manager_ProcessRx(void) {
    uint16_t i = 0;
    uint16_t processed_len = 0;
    
    while (i + 11 <= g_LoRaManager.rx_len) {
        if (g_LoRaManager.RxBuffer[i] == PROTOCOL_HEAD_0 && 
            g_LoRaManager.RxBuffer[i+1] == PROTOCOL_HEAD_1) {
            
            uint8_t p_len = g_LoRaManager.RxBuffer[i+2];
            uint8_t ctrl = g_LoRaManager.RxBuffer[i+3];
            bool has_crc = (ctrl & CTRL_MASK_HAS_CRC);
            uint16_t pkt_len = 9 + p_len + (has_crc ? 2 : 0) + 2;
            
            if (i + pkt_len > g_LoRaManager.rx_len) break;
            
            if (g_LoRaManager.RxBuffer[i + pkt_len - 2] == PROTOCOL_TAIL_0 && 
                g_LoRaManager.RxBuffer[i + pkt_len - 1] == PROTOCOL_TAIL_1) {
                
                uint16_t target = (uint16_t)g_LoRaManager.RxBuffer[i+5] | (g_LoRaManager.RxBuffer[i+6] << 8);
                uint16_t src    = (uint16_t)g_LoRaManager.RxBuffer[i+7] | (g_LoRaManager.RxBuffer[i+8] << 8);
                uint8_t  seq    = g_LoRaManager.RxBuffer[i+4];
                
                bool accept = (target == g_LoRaManager.local_id) || 
                              (target == 0xFFFF) || 
                              (g_LoRaManager.group_id != 0 && target == g_LoRaManager.group_id);
                
                if (accept) {
                    bool crc_ok = true;
                    if (has_crc) {
                        uint16_t calc = CRC16_Calc(&g_LoRaManager.RxBuffer[i+2], pkt_len - 6);
                        uint16_t recv = (uint16_t)g_LoRaManager.RxBuffer[i + pkt_len - 4] | 
                                        (g_LoRaManager.RxBuffer[i + pkt_len - 3] << 8);
                        if (calc != recv) crc_ok = false;
                    }
                    
                    if (crc_ok) {
                        if (ctrl & CTRL_MASK_TYPE) { // ACK
                            if (g_LoRaManager.state == MGR_STATE_WAIT_ACK) {
                                LORA_LOG("[MGR] ACK Recv from %d\r\n", src);
                                if (g_LoRaManager.cb_on_tx) g_LoRaManager.cb_on_tx(true);
                                Manager_ResetState();
                            }
                        } else { // Data
                            if (g_LoRaManager.cb_on_rx) {
                                g_LoRaManager.cb_on_rx(&g_LoRaManager.RxBuffer[i+9], p_len, src);
                            }
                            // 触发 ACK 延时逻辑
                            if ((ctrl & CTRL_MASK_NEED_ACK) && target != 0xFFFF) {
                                _PrepareAck(src, seq);
                            }
                        }
                    } else {
                        if (g_LoRaManager.cb_on_err) g_LoRaManager.cb_on_err(LORA_ERR_CRC_FAIL);
                    }
                }
                i += pkt_len;
                processed_len = i;
            } else { i++; processed_len = i; }
        } else { i++; processed_len = i; }
    }
    
    if (processed_len > 0) {
        if (processed_len < g_LoRaManager.rx_len) {
            memmove(g_LoRaManager.RxBuffer, &g_LoRaManager.RxBuffer[processed_len], g_LoRaManager.rx_len - processed_len);
            g_LoRaManager.rx_len -= processed_len;
        } else { g_LoRaManager.rx_len = 0; }
    }
}

// --- 核心接口 ---

void Manager_Init(OnRxData_t on_rx, OnTxResult_t on_tx, OnError_t on_err) {
    g_LoRaManager.cb_on_rx = on_rx;
    g_LoRaManager.cb_on_tx = on_tx;
    g_LoRaManager.cb_on_err = on_err;
    Manager_ResetState();
}

bool Manager_SendPacket(const uint8_t *payload, uint16_t len, uint16_t target_id) {
    if (g_LoRaManager.state != MGR_STATE_IDLE) return false;
    
    uint8_t *p = g_LoRaManager.TxBuffer;
    uint16_t idx = 0;
    
    if (g_LoRaConfig_Current.tmode == 1) {
        p[idx++] = (uint8_t)(target_id >> 8);
        p[idx++] = (uint8_t)(target_id & 0xFF);
        p[idx++] = g_LoRaConfig_Current.channel;
    }
    
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
        uint16_t crc_start = (g_LoRaConfig_Current.tmode == 1) ? 3 : 0;
        uint16_t crc = CRC16_Calc(&p[crc_start + 2], idx - crc_start - 2);
        p[idx++] = (uint8_t)(crc & 0xFF);
        p[idx++] = (uint8_t)(crc >> 8);
    }
    
    p[idx++] = PROTOCOL_TAIL_0;
    p[idx++] = PROTOCOL_TAIL_1;
    
    s_CurrentTxLen = idx;
    LORA_HEXDUMP("[MGR] TX RAW", p, idx);
    
    g_LoRaManager.is_sending_ack = false; // 标记为普通数据,markA
		
    // 尝试发送
    if (_PhySend(p, idx)) {
        g_LoRaManager.retry_cnt = 0;
        return true;
    }
    return false;
}

void Manager_Run(void) {
    uint32_t now = OSAL_GetTick();
    
    // 1. RX 处理
    uint16_t read = Drv_Read(&g_LoRaManager.RxBuffer[g_LoRaManager.rx_len], MGR_RX_BUF_SIZE - g_LoRaManager.rx_len);
    if (read > 0) {
        LORA_HEXDUMP("[MGR] RX RAW", &g_LoRaManager.RxBuffer[g_LoRaManager.rx_len], read);
        g_LoRaManager.rx_len += read;
        Manager_ProcessRx();
    }
    
    // 2. 状态机
    switch (g_LoRaManager.state) {
        case MGR_STATE_IDLE: break;
            
        case MGR_STATE_ACK_DELAY:
            // 延时等待发送 ACK
            if (now - g_LoRaManager.state_tick > LORA_ACK_DELAY_MS) {
                if (!Drv_IsBusy()) {
                    _SendPendingAck(); // 发送后切到 TX_RUNNING
                }
                // 如果忙，下一轮再试，不阻塞
            }
            break;
            
        case MGR_STATE_TX_RUNNING:
            if (!Drv_IsBusy()) {
                // 发送完成
                
                // [修复] 如果是 ACK 包，发送完直接回 IDLE，绝不等待 ACK
                if (g_LoRaManager.is_sending_ack) {
                    Manager_ResetState();
                } 
                else {
                    // 普通数据包，检查 TxBuffer 中的控制位
                    uint8_t ctrl_offset = (g_LoRaConfig_Current.tmode == 1) ? 6 : 3;
                    if (g_LoRaManager.TxBuffer[ctrl_offset] & CTRL_MASK_NEED_ACK) {
                        g_LoRaManager.state = MGR_STATE_WAIT_ACK;
                        g_LoRaManager.state_tick = now;
                    } else {
                        if (g_LoRaManager.cb_on_tx) g_LoRaManager.cb_on_tx(true);
                        Manager_ResetState();
                    }
                }
            }
						
            else if (now - g_LoRaManager.state_tick > LORA_TX_TIMEOUT_MS) {
                // 物理发送超时 (硬件死锁)
                if (g_LoRaManager.cb_on_err) g_LoRaManager.cb_on_err(LORA_ERR_TX_TIMEOUT);
                Manager_ResetState();
            }
            break;
            
        case MGR_STATE_WAIT_ACK:
            if (now - g_LoRaManager.state_tick > LORA_ACK_TIMEOUT_MS) {
                if (g_LoRaManager.retry_cnt < LORA_MAX_RETRY) {
                    g_LoRaManager.retry_cnt++;
                    LORA_LOG("[MGR] Retry %d\r\n", g_LoRaManager.retry_cnt);
                    if (_PhySend(g_LoRaManager.TxBuffer, s_CurrentTxLen)) {
                        // 重传成功启动
                    } else {
                        // 忙，下轮再试
                    }
                } else {
                    if (g_LoRaManager.cb_on_err) g_LoRaManager.cb_on_err(LORA_ERR_ACK_TIMEOUT);
                    if (g_LoRaManager.cb_on_tx) g_LoRaManager.cb_on_tx(false);
                    Manager_ResetState();
                }
            }
            break;
    }
}
