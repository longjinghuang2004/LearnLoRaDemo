#include "lora_manager.h"
#include "mod_lora.h"
#include "lora_port.h"
#include "Delay.h"
#include <string.h>

#if LORA_DEBUG_PRINT
    #include "Serial.h"
    #include <stdio.h>
    #define LOG_DBG(...) printf(__VA_ARGS__)
#else
    #define LOG_DBG(...)
#endif

// 引入全局配置以获取 TMODE 和 Channel
extern LoRa_Config_t g_LoRaConfig_Current; 

LoRa_Manager_t g_LoRaManager;
static uint16_t s_CurrentTxLen = 0;

// --- 内部函数声明 ---
static uint16_t CRC16_Calc(const uint8_t *data, uint16_t len);
static void Manager_ProcessRx(void);
static void Manager_SendACK(uint16_t target_id, uint8_t seq);
static void Manager_ResetState(void);

static void Manager_DumpHex(const char* tag, uint8_t* buf, uint16_t len) {
#if LORA_DEBUG_PRINT
    printf("%s (%d): ", tag, len);
    for(uint16_t i=0; i<len; i++) printf("%02X ", buf[i]);
    printf("\r\n");
#endif
}

// [修改] 初始化函数
void Manager_Init(OnRxData_t on_rx, OnTxResult_t on_tx, OnError_t on_err)
{
    LoRa_Core_Init();
    Port_ClearRxBuffer();
    
    // 注意：local_id 和 uuid 应该在 Init 之前由 App 层赋值给 g_LoRaManager
    // 这里不再硬编码 local_id
    
    g_LoRaManager.cb_on_rx = on_rx;
    g_LoRaManager.cb_on_tx = on_tx;
    g_LoRaManager.cb_on_err = on_err;
    
    Manager_ResetState();
    g_LoRaManager.tx_seq = 0;
    g_LoRaManager.rx_len = 0; 
    
    LOG_DBG("[MGR] Init Done. NetID:0x%04X, UUID:0x%08X\r\n", 
            g_LoRaManager.local_id, g_LoRaManager.uuid);
}

bool Manager_SendPacket(const uint8_t *payload, uint16_t len, uint16_t target_id)
{
    if (g_LoRaManager.state != MGR_STATE_IDLE) return false;
    
    // 计算开销：协议头(11) + CRC(2) + 定向头(3, 如果是定向模式)
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
    
    // [新增] 如果是定向模式，插入 3 字节定向头
    if (g_LoRaConfig_Current.tmode == LORA_TMODE_FIXED) {
        p[idx++] = (uint8_t)(target_id >> 8);   // Target High
        p[idx++] = (uint8_t)(target_id & 0xFF); // Target Low
        p[idx++] = g_LoRaConfig_Current.channel;// Target Channel (同频)
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
        // CRC 计算范围：从 Protocol Head 开始，不包含定向头
        uint16_t crc_start_idx = (g_LoRaConfig_Current.tmode == LORA_TMODE_FIXED) ? 3 : 0;
        // 跳过 Head0, Head1 (前2字节)
        uint16_t crc = CRC16_Calc(&p[crc_start_idx + 2], idx - crc_start_idx - 2);
        p[idx++] = (uint8_t)(crc & 0xFF);
        p[idx++] = (uint8_t)(crc >> 8);
    }
    
    p[idx++] = PROTOCOL_TAIL_0;
    p[idx++] = PROTOCOL_TAIL_1;
    
    s_CurrentTxLen = idx;
    Manager_DumpHex("[MGR] TX RAW", p, idx);
    
    g_LoRaManager.state = MGR_STATE_TX_CHECK_AUX;
    g_LoRaManager.state_tick = Port_GetTick();
    g_LoRaManager.retry_cnt = 0;
    
    return true;
}

void Manager_Run(void)
{
    uint32_t now = Port_GetTick();
    
    // RX 处理
    uint16_t read_len = LoRa_Core_ReadRaw(
        &g_LoRaManager.RxBuffer[g_LoRaManager.rx_len], 
        MGR_RX_BUF_SIZE - g_LoRaManager.rx_len
    );
    if (read_len > 0) {
        Manager_DumpHex("[MGR] RX RAW", &g_LoRaManager.RxBuffer[g_LoRaManager.rx_len], read_len);
        g_LoRaManager.rx_len += read_len;
        Manager_ProcessRx();
    }
    
    // TX 状态机
    switch (g_LoRaManager.state)
    {
        case MGR_STATE_IDLE: break;
            
        case MGR_STATE_TX_CHECK_AUX:
            if (!LoRa_Core_IsBusy()) {
                LoRa_Core_SendRaw(g_LoRaManager.TxBuffer, s_CurrentTxLen);
                g_LoRaManager.state = MGR_STATE_TX_SENDING;
                g_LoRaManager.state_tick = now;
            }
            else if (now - g_LoRaManager.state_tick > LORA_TX_TIMEOUT_MS) {
                LOG_DBG("[MGR] Err: AUX Timeout (Check Wiring!)\r\n");
                if (g_LoRaManager.cb_on_err) g_LoRaManager.cb_on_err(LORA_ERR_TX_TIMEOUT);
                Manager_ResetState();
            }
            break;
            
        case MGR_STATE_TX_SENDING:
            // 检查是否需要 ACK (注意：定向头会偏移 Ctrl 字节的位置)
            {
                uint8_t ctrl_offset = (g_LoRaConfig_Current.tmode == LORA_TMODE_FIXED) ? 3+3 : 3;
                if (g_LoRaManager.TxBuffer[ctrl_offset] & CTRL_MASK_NEED_ACK) {
                    g_LoRaManager.state = MGR_STATE_WAIT_ACK;
                    g_LoRaManager.state_tick = now;
                    LOG_DBG("[MGR] Waiting ACK...\r\n");
                } else {
                    if (g_LoRaManager.cb_on_tx) g_LoRaManager.cb_on_tx(true);
                    Manager_ResetState();
                }
            }
            break;
            
        case MGR_STATE_WAIT_ACK:
            if (now - g_LoRaManager.state_tick > LORA_ACK_TIMEOUT_MS) {
                if (g_LoRaManager.retry_cnt < LORA_MAX_RETRY) {
                    g_LoRaManager.retry_cnt++;
                    LOG_DBG("[MGR] ACK Timeout. Retry %d\r\n", g_LoRaManager.retry_cnt);
                    g_LoRaManager.state = MGR_STATE_TX_CHECK_AUX;
                    g_LoRaManager.state_tick = now;
                } else {
                    LOG_DBG("[MGR] Err: ACK Failed\r\n");
                    if (g_LoRaManager.cb_on_err) g_LoRaManager.cb_on_err(LORA_ERR_ACK_TIMEOUT);
                    if (g_LoRaManager.cb_on_tx) g_LoRaManager.cb_on_tx(false);
                    Manager_ResetState();
                }
            }
            break;
    }
}

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
        // 1. 寻找包头
        if (g_LoRaManager.RxBuffer[i] == PROTOCOL_HEAD_0 && 
            g_LoRaManager.RxBuffer[i+1] == PROTOCOL_HEAD_1) {
            
            uint8_t payload_len = g_LoRaManager.RxBuffer[i+2];
            uint8_t ctrl = g_LoRaManager.RxBuffer[i+3];
            bool has_crc = (ctrl & CTRL_MASK_HAS_CRC) ? true : false;
            
            // 计算整包长度
            uint16_t packet_len = 2 + 1 + 1 + 1 + 2 + 2 + payload_len + (has_crc ? 2 : 0) + 2;
            
            if (i + packet_len > g_LoRaManager.rx_len) break; // 数据未收全，等待
            
            // 2. 验证包尾
            if (g_LoRaManager.RxBuffer[i + packet_len - 2] == PROTOCOL_TAIL_0 && 
                g_LoRaManager.RxBuffer[i + packet_len - 1] == PROTOCOL_TAIL_1) {
                
                // 3. 提取地址信息
                uint16_t target_id = (uint16_t)g_LoRaManager.RxBuffer[i+5] | ((uint16_t)g_LoRaManager.RxBuffer[i+6] << 8);
                uint16_t src_id    = (uint16_t)g_LoRaManager.RxBuffer[i+7] | ((uint16_t)g_LoRaManager.RxBuffer[i+8] << 8);
                uint8_t  seq       = g_LoRaManager.RxBuffer[i+4];
                
                // 4. [核心修改] 身份过滤逻辑
                bool accept = false;

                // 规则A: 匹配我的逻辑 ID (NetID)
                if (target_id == g_LoRaManager.local_id) {
                    accept = true;
                }
                // 规则B: 匹配广播 ID (0xFFFF)
                else if (target_id == LORA_ID_BROADCAST) {
                    accept = true;
                }
                // 规则C: 我是未分配设备(ID=0)，且收到了发给未分配设备(ID=0)的包
                //        (此时 App 层会进一步解析 Payload 里的 UUID 来确认是不是找我的)
                else if (g_LoRaManager.local_id == LORA_ID_UNASSIGNED && target_id == LORA_ID_UNASSIGNED) {
                    accept = true;
                }

                if (accept) {
                    bool crc_ok = true;
                    if (has_crc) {
                        uint16_t calc_len = 1 + 1 + 1 + 4 + payload_len;
                        uint16_t calc_crc = CRC16_Calc(&g_LoRaManager.RxBuffer[i+2], calc_len);
                        uint16_t recv_crc = (uint16_t)g_LoRaManager.RxBuffer[i + packet_len - 4] | 
                                            ((uint16_t)g_LoRaManager.RxBuffer[i + packet_len - 3] << 8);
                        
                        if (calc_crc != recv_crc) {
                            crc_ok = false;
                            LOG_DBG("[MGR] CRC Fail! Calc:0x%04X Recv:0x%04X\r\n", calc_crc, recv_crc);
                            if (g_LoRaManager.cb_on_err) g_LoRaManager.cb_on_err(LORA_ERR_CRC_FAIL);
                        }
                    }
                    
                    if (crc_ok) {
                        // 检查是否是 ACK 包
                        if ((ctrl & CTRL_MASK_TYPE) != 0) {
                            if (g_LoRaManager.state == MGR_STATE_WAIT_ACK) {
                                LOG_DBG("[MGR] ACK Recv from 0x%04X\r\n", src_id);
                                if (g_LoRaManager.cb_on_tx) g_LoRaManager.cb_on_tx(true);
                                Manager_ResetState();
                            }
                        } else {
                            // 是数据包
                            uint8_t *p_payload = &g_LoRaManager.RxBuffer[i + 9];
                            if (g_LoRaManager.cb_on_rx) {
                                g_LoRaManager.cb_on_rx(p_payload, payload_len, src_id);
                            }
                            // 回复 ACK (广播包和未分配包通常不回ACK，防止风暴)
                            if ((ctrl & CTRL_MASK_NEED_ACK) && target_id != LORA_ID_BROADCAST && target_id != LORA_ID_UNASSIGNED) {
                                Manager_SendACK(src_id, seq);
                            }
                        }
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
    Delay_ms(LORA_ACK_DELAY_MS); 
    
    uint8_t ack_buf[32]; // 稍微加大一点以容纳定向头
    uint16_t idx = 0;
    
    // [新增] 定向模式下的 ACK 也需要定向头
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
        // CRC 计算范围：从 Protocol Head 开始
        uint16_t crc_start_idx = (g_LoRaConfig_Current.tmode == LORA_TMODE_FIXED) ? 3 : 0;
        uint16_t crc = CRC16_Calc(&ack_buf[crc_start_idx + 2], idx - crc_start_idx - 2);
        ack_buf[idx++] = (uint8_t)(crc & 0xFF);
        ack_buf[idx++] = (uint8_t)(crc >> 8);
    }
    
    ack_buf[idx++] = PROTOCOL_TAIL_0;
    ack_buf[idx++] = PROTOCOL_TAIL_1;
    
    Manager_DumpHex("[MGR] TX ACK", ack_buf, idx);
    LoRa_Core_SendRaw(ack_buf, idx);
}
