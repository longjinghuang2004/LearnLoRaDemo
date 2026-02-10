#include "lora_manager.h"
#include "mod_lora.h"
#include "lora_port.h"
#include "Delay.h" // 需要延时函数
#include <string.h>

// 调试打印宏
#if LORA_DEBUG_PRINT
    #include "Serial.h"
    #include <stdio.h>
    #define LOG_DBG(...) printf(__VA_ARGS__)
#else
    #define LOG_DBG(...)
#endif

// 全局单例
LoRa_Manager_t g_LoRaManager;

// --- 内部辅助函数声明 ---
static uint16_t CRC16_Calc(const uint8_t *data, uint16_t len);
static void Manager_ProcessRx(void);
static void Manager_SendACK(uint16_t target_id, uint8_t seq);
static void Manager_ResetState(void);

// [新增] 16进制打印辅助函数
static void Manager_DumpHex(const char* tag, uint8_t* buf, uint16_t len)
{
#if LORA_DEBUG_PRINT
    printf("%s (%d): ", tag, len);
    for(uint16_t i=0; i<len; i++) {
        printf("%02X ", buf[i]);
    }
    printf("\r\n");
#endif
}

// ============================================================
//                    接口实现
// ============================================================

void Manager_Init(ManagerConfig_t *cfg, OnRxData_t on_rx, OnTxResult_t on_tx, OnError_t on_err)
{
    LoRa_Core_Init();
    
    if (cfg) g_LoRaManager.config = *cfg;
    else {
        g_LoRaManager.config.local_id = 0x0001;
        g_LoRaManager.config.enable_crc = true;
        g_LoRaManager.config.enable_ack = true;
    }
    
    g_LoRaManager.cb_on_rx = on_rx;
    g_LoRaManager.cb_on_tx = on_tx;
    g_LoRaManager.cb_on_err = on_err;
    
    Manager_ResetState();
    g_LoRaManager.tx_seq = 0;
    
    LOG_DBG("[MGR] Init Done. ID:0x%04X\r\n", g_LoRaManager.config.local_id);
}

void Manager_EnableCRC(bool enable)
{
    g_LoRaManager.config.enable_crc = enable;
}

// 静态变量暂存发送长度
static uint16_t s_CurrentTxLen = 0;

bool Manager_SendPacket(const uint8_t *payload, uint16_t len, uint16_t target_id)
{
    if (g_LoRaManager.state != MGR_STATE_IDLE) {
        LOG_DBG("[MGR] Send Fail: Busy\r\n");
        return false;
    }
    
    uint16_t overhead = 11 + (g_LoRaManager.config.enable_crc ? 2 : 0);
    if (len + overhead > MGR_TX_BUF_SIZE) {
        if (g_LoRaManager.cb_on_err) g_LoRaManager.cb_on_err(LORA_ERR_MEM_OVERFLOW);
        return false;
    }
    
    uint8_t *p = g_LoRaManager.TxBuffer;
    uint16_t idx = 0;
    
    p[idx++] = PROTOCOL_HEAD_0;
    p[idx++] = PROTOCOL_HEAD_1;
    p[idx++] = (uint8_t)len;
    
    uint8_t ctrl = 0;
    if (g_LoRaManager.config.enable_ack) ctrl |= CTRL_MASK_NEED_ACK;
    if (g_LoRaManager.config.enable_crc) ctrl |= CTRL_MASK_HAS_CRC;
    p[idx++] = ctrl;
    
    p[idx++] = ++g_LoRaManager.tx_seq;
    
    p[idx++] = (uint8_t)(target_id & 0xFF);
    p[idx++] = (uint8_t)(target_id >> 8);
    p[idx++] = (uint8_t)(g_LoRaManager.config.local_id & 0xFF);
    p[idx++] = (uint8_t)(g_LoRaManager.config.local_id >> 8);
    
    memcpy(&p[idx], payload, len);
    idx += len;
    
    if (g_LoRaManager.config.enable_crc) {
        uint16_t crc = CRC16_Calc(&p[2], idx - 2);
        p[idx++] = (uint8_t)(crc & 0xFF);
        p[idx++] = (uint8_t)(crc >> 8);
    }
    
    p[idx++] = PROTOCOL_TAIL_0;
    p[idx++] = PROTOCOL_TAIL_1;
    
    s_CurrentTxLen = idx;
    
    // [调试] 打印即将发送的包
    Manager_DumpHex("[MGR] TX RAW", p, idx);
    
    g_LoRaManager.state = MGR_STATE_TX_CHECK_AUX;
    g_LoRaManager.state_tick = Port_GetTick();
    g_LoRaManager.retry_cnt = 0;
    
    return true;
}

void Manager_Run(void)
{
    uint32_t now = Port_GetTick();
    
    // ------------------------------------------------
    // 1. 接收处理
    // ------------------------------------------------
    uint16_t read_len = LoRa_Core_ReadRaw(
        &g_LoRaManager.RxBuffer[g_LoRaManager.rx_len], 
        MGR_RX_BUF_SIZE - g_LoRaManager.rx_len
    );
    
    if (read_len > 0) {
        // [调试] 打印刚收到的原始字节
        Manager_DumpHex("[MGR] RX RAW", &g_LoRaManager.RxBuffer[g_LoRaManager.rx_len], read_len);
        
        g_LoRaManager.rx_len += read_len;
        Manager_ProcessRx();
    }
    
    // ------------------------------------------------
    // 2. 发送状态机
    // ------------------------------------------------
    switch (g_LoRaManager.state)
    {
        case MGR_STATE_IDLE:
            break;
            
        case MGR_STATE_TX_CHECK_AUX:
            if (!LoRa_Core_IsBusy()) {
                LoRa_Core_SendRaw(g_LoRaManager.TxBuffer, s_CurrentTxLen);
                g_LoRaManager.state = MGR_STATE_TX_SENDING;
                g_LoRaManager.state_tick = now;
            }
            else if (now - g_LoRaManager.state_tick > TIMEOUT_TX_AUX) {
                LOG_DBG("[MGR] Err: AUX Timeout\r\n");
                if (g_LoRaManager.cb_on_err) g_LoRaManager.cb_on_err(LORA_ERR_TX_TIMEOUT);
                Manager_ResetState();
            }
            break;
            
        case MGR_STATE_TX_SENDING:
            // 简单延时确保DMA启动
            if (g_LoRaManager.TxBuffer[3] & CTRL_MASK_NEED_ACK) {
                g_LoRaManager.state = MGR_STATE_WAIT_ACK;
                g_LoRaManager.state_tick = now;
                LOG_DBG("[MGR] Waiting ACK...\r\n");
            } else {
                if (g_LoRaManager.cb_on_tx) g_LoRaManager.cb_on_tx(true);
                Manager_ResetState();
            }
            break;
            
        case MGR_STATE_WAIT_ACK:
            if (now - g_LoRaManager.state_tick > TIMEOUT_WAIT_ACK) {
                if (g_LoRaManager.retry_cnt < MAX_RETRY_COUNT) {
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

// ============================================================
//                    内部逻辑
// ============================================================

static void Manager_ResetState(void)
{
    g_LoRaManager.state = MGR_STATE_IDLE;
}

static uint16_t CRC16_Calc(const uint8_t *data, uint16_t len)
{
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

static void Manager_ProcessRx(void)
{
    uint8_t *buf = g_LoRaManager.RxBuffer;
    uint16_t len = g_LoRaManager.rx_len;
    uint16_t processed = 0;
    
    while (len - processed >= 11) {
        uint16_t head_idx = processed;
        
        // 1. 找包头
        if (buf[head_idx] != PROTOCOL_HEAD_0 || buf[head_idx+1] != PROTOCOL_HEAD_1) {
            processed++;
            continue;
        }
        
        // 2. 获取 Payload 长度
        uint8_t payload_len = buf[head_idx+2];
        uint8_t ctrl = buf[head_idx+3];
        bool has_crc = (ctrl & CTRL_MASK_HAS_CRC);
        
        uint16_t total_len = 11 + payload_len + (has_crc ? 2 : 0);
        
        if (len - processed < total_len) {
            break; 
        }
        
        // 3. 检查包尾
        if (buf[head_idx + total_len - 2] != PROTOCOL_TAIL_0 || 
            buf[head_idx + total_len - 1] != PROTOCOL_TAIL_1) {
            LOG_DBG("[MGR] Bad Tail. Skip.\r\n");
            processed++;
            continue;
        }
        
        // 4. CRC 校验
        if (has_crc) {
            uint16_t calc_len = 7 + payload_len;
            uint16_t cal_crc = CRC16_Calc(&buf[head_idx+2], calc_len);
            uint16_t recv_crc = buf[head_idx + total_len - 4] | (buf[head_idx + total_len - 3] << 8);
            
            if (cal_crc != recv_crc) {
                LOG_DBG("[MGR] CRC Fail. Calc:0x%04X Recv:0x%04X\r\n", cal_crc, recv_crc);
                processed += total_len;
                continue;
            }
        }
        
        // --- 包有效 ---
        uint8_t seq = buf[head_idx+4];
        uint16_t tgt_id = buf[head_idx+5] | (buf[head_idx+6] << 8);
        uint16_t src_id = buf[head_idx+7] | (buf[head_idx+8] << 8);
        bool is_ack = (ctrl & CTRL_MASK_TYPE);
        bool need_ack = (ctrl & CTRL_MASK_NEED_ACK);
        
        // 5. 地址过滤
        if (tgt_id == g_LoRaManager.config.local_id || tgt_id == 0xFFFF) {
            
            if (is_ack) {
                if (g_LoRaManager.state == MGR_STATE_WAIT_ACK && seq == g_LoRaManager.tx_seq) {
                    LOG_DBG("[MGR] ACK Recv from 0x%04X\r\n", src_id);
                    if (g_LoRaManager.cb_on_tx) g_LoRaManager.cb_on_tx(true);
                    Manager_ResetState();
                }
            } else {
                LOG_DBG("[MGR] Data Recv from 0x%04X. Len:%d\r\n", src_id, payload_len);
                if (g_LoRaManager.cb_on_rx) {
                    g_LoRaManager.cb_on_rx(&buf[head_idx+9], payload_len, src_id);
                }
                if (need_ack && tgt_id != 0xFFFF) {
                    Manager_SendACK(src_id, seq);
                }
            }
        } else {
            LOG_DBG("[MGR] Ignore Packet for 0x%04X\r\n", tgt_id);
        }
        
        processed += total_len;
    }
    
    if (processed > 0) {
        if (processed < len) {
            memmove(g_LoRaManager.RxBuffer, &g_LoRaManager.RxBuffer[processed], len - processed);
            g_LoRaManager.rx_len -= processed;
        } else {
            g_LoRaManager.rx_len = 0;
        }
    }
}

static void Manager_SendACK(uint16_t target_id, uint8_t seq)
{
    // [新增] 延时 20ms，给主机切换 RX 状态的时间
    Delay_ms(20);
    
    uint8_t ack_buf[16];
    uint16_t idx = 0;
    
    ack_buf[idx++] = PROTOCOL_HEAD_0;
    ack_buf[idx++] = PROTOCOL_HEAD_1;
    ack_buf[idx++] = 0; 
    
    uint8_t ctrl = CTRL_MASK_TYPE; 
    if (g_LoRaManager.config.enable_crc) ctrl |= CTRL_MASK_HAS_CRC;
    ack_buf[idx++] = ctrl;
    
    ack_buf[idx++] = seq; 
    
    ack_buf[idx++] = (uint8_t)(target_id & 0xFF);
    ack_buf[idx++] = (uint8_t)(target_id >> 8);
    ack_buf[idx++] = (uint8_t)(g_LoRaManager.config.local_id & 0xFF);
    ack_buf[idx++] = (uint8_t)(g_LoRaManager.config.local_id >> 8);
    
    if (g_LoRaManager.config.enable_crc) {
        uint16_t crc = CRC16_Calc(&ack_buf[2], idx - 2);
        ack_buf[idx++] = (uint8_t)(crc & 0xFF);
        ack_buf[idx++] = (uint8_t)(crc >> 8);
    }
    
    ack_buf[idx++] = PROTOCOL_TAIL_0;
    ack_buf[idx++] = PROTOCOL_TAIL_1;
    
    if (!LoRa_Core_IsBusy()) {
        // [调试] 打印 ACK 包
        Manager_DumpHex("[MGR] TX ACK", ack_buf, idx);
        LoRa_Core_SendRaw(ack_buf, idx);
    }
}
