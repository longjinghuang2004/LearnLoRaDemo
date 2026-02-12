#include "lora_manager.h"
#include "lora_driver.h"
#include "lora_port.h"
#include <string.h>

// --- 外部依赖声明 (解耦 Service 层) ---
extern const LoRa_Config_t* Service_GetConfig(void);
extern uint32_t Port_GetTick(void);
extern bool Drv_IsIdle(void);

// --- 调试宏定义 ---
#if LORA_DEBUG_PRINT
    #include "Serial.h"
    #include <stdio.h>
    #define LOG_PRINT(...)  Serial_Printf(__VA_ARGS__)
    
    static void _Log_Hex(const char *tag, uint8_t *buf, uint16_t len) {
        Serial_Printf("%s (%d): ", tag, len);
        for(uint16_t i=0; i<len; i++) Serial_Printf("%02X ", buf[i]);
        Serial_Printf("\r\n");
    }
    #define LOG_HEX(tag, buf, len) _Log_Hex(tag, buf, len)
#else
    #define LOG_PRINT(...)
    #define LOG_HEX(tag, buf, len)
#endif

LoRa_Manager_t g_LoRaManager;

// 内部函数声明
static uint16_t _CRC16_Calc(const uint8_t *data, uint16_t len);
static void _Mgr_ProcessRx(void);
static void _Mgr_TrySendPendingACK(void);
static void _Mgr_ResetState(void);

// ============================================================
//                    1. 初始化
// ============================================================
void Manager_Init(uint16_t local_id, uint16_t group_id, 
                  OnMgrRecv_t on_rx, OnMgrTxResult_t on_tx, OnMgrError_t on_err)
{
    memset(&g_LoRaManager, 0, sizeof(g_LoRaManager));
    g_LoRaManager.local_id = local_id;
    g_LoRaManager.group_id = group_id;
    g_LoRaManager.cb_on_rx = on_rx;
    g_LoRaManager.cb_on_tx = on_tx;
    g_LoRaManager.cb_on_err = on_err;
    _Mgr_ResetState();
    LOG_PRINT("[MGR] Init. ID:0x%04X Group:0x%04X\r\n", local_id, group_id);
}

// ============================================================
//                    2. 发送逻辑 (TMODE & CRC)
// ============================================================
LoRa_Result_t Manager_Send(const uint8_t *payload, uint16_t len, uint16_t target_id, bool need_ack)
{
    if (g_LoRaManager.state != MGR_STATE_IDLE) return LORA_ERR_BUSY;
    
    // 1. 计算开销
    uint16_t overhead = 9; // 基础头
    if (LORA_ENABLE_CRC) overhead += 2;
    
    const LoRa_Config_t *cfg = Service_GetConfig();
    bool is_fixed = (cfg->tmode == LORA_TMODE_FIXED);
    if (is_fixed) overhead += 3;

    if (len + overhead > MGR_TX_BUF_SIZE) return LORA_ERR_PARAM;
    
    uint8_t *p = g_LoRaManager.tx_buf;
    uint16_t idx = 0;
    
    // 2. [TMODE] 插入定向头
    if (is_fixed) {
        p[idx++] = (uint8_t)(target_id >> 8);
        p[idx++] = (uint8_t)(target_id & 0xFF);
        p[idx++] = cfg->channel;
    }
    
    // 3. 协议头
    p[idx++] = 'C';
    p[idx++] = 'M';
    p[idx++] = (uint8_t)len;
    
    uint8_t ctrl = 0;
    if (need_ack) ctrl |= 0x40;
    if (LORA_ENABLE_CRC) ctrl |= 0x20;
    p[idx++] = ctrl;
    
    p[idx++] = ++g_LoRaManager.tx_seq;
    
    // 目标与源
    p[idx++] = (uint8_t)(target_id & 0xFF);
    p[idx++] = (uint8_t)(target_id >> 8);
    p[idx++] = (uint8_t)(g_LoRaManager.local_id & 0xFF);
    p[idx++] = (uint8_t)(g_LoRaManager.local_id >> 8);
    
    // Payload
    memcpy(&p[idx], payload, len);
    idx += len;
    
    // 4. [CRC] 计算
    if (LORA_ENABLE_CRC) {
        uint16_t crc_start = is_fixed ? 3 : 0;
        uint16_t crc = _CRC16_Calc(&p[crc_start], idx - crc_start);
        p[idx++] = (uint8_t)(crc & 0xFF);
        p[idx++] = (uint8_t)(crc >> 8);
    }
    
    g_LoRaManager.tx_len = idx;
    
    // 5. 发送
    LOG_HEX("[MGR] TX RAW", p, idx);
    LoRa_Result_t res = Drv_AsyncSend(p, idx);
    
    if (res == LORA_OK) {
        if (need_ack) {
            g_LoRaManager.state = MGR_STATE_WAIT_ACK;
            g_LoRaManager.state_tick = Port_GetTick();
            g_LoRaManager.retry_cnt = 0;
        } else {
            g_LoRaManager.state = MGR_STATE_TX_SENDING;
        }
    }
    return res;
}

// ============================================================
//                    3. 接收与 ACK 逻辑
// ============================================================
static void _Mgr_ProcessRx(void) {
    uint8_t *buf = g_LoRaManager.rx_buf;
    uint16_t len = g_LoRaManager.rx_len;
    
    if (len < 5) return;
    LOG_HEX("[MGR] RX RAW", buf, len);
    
    // 1. 寻找包头
    uint16_t i = 0;
    while (i < len - 1) {
        if (buf[i] == 'C' && buf[i+1] == 'M') break;
        i++;
    }
    if (i >= len - 1) return;
    
    // 2. 解析
    uint8_t payload_len = buf[i+2];
    uint8_t ctrl = buf[i+3];
    uint8_t seq  = buf[i+4];
    uint16_t dst = (uint16_t)buf[i+5] | ((uint16_t)buf[i+6] << 8);
    uint16_t src = (uint16_t)buf[i+7] | ((uint16_t)buf[i+8] << 8);
    
    bool has_crc = (ctrl & 0x20);
    bool need_ack = (ctrl & 0x40);
    bool is_ack_pkg = (ctrl & 0x80);
    
    // 3. 校验 CRC
    uint16_t header_len = 9;
    uint16_t total_len = header_len + payload_len + (has_crc ? 2 : 0);
    if (i + total_len > len) return;
    
    if (has_crc) {
        uint16_t calc_crc = _CRC16_Calc(&buf[i], header_len + payload_len);
        uint16_t recv_crc = (uint16_t)buf[i + total_len - 2] | ((uint16_t)buf[i + total_len - 1] << 8);
        if (calc_crc != recv_crc) {
            LOG_PRINT("[MGR] CRC Fail!\r\n");
            if (g_LoRaManager.cb_on_err) g_LoRaManager.cb_on_err(LORA_ERR_CRC);
            return;
        }
    }
    
    // 4. 过滤
    bool accept = (dst == g_LoRaManager.local_id) || (dst == 0xFFFF) || 
                  (g_LoRaManager.group_id != 0 && dst == g_LoRaManager.group_id);
    if (!accept) return;
    
    // 5. 处理
    if (is_ack_pkg) {
        if (g_LoRaManager.state == MGR_STATE_WAIT_ACK) {
            LOG_PRINT("[MGR] ACK Recv\r\n");
            g_LoRaManager.state = MGR_STATE_IDLE;
            if (g_LoRaManager.cb_on_tx) g_LoRaManager.cb_on_tx(true);
        }
    } else {
        // [核心] 挂起 ACK，不立即发送
        if (need_ack && dst != 0xFFFF) {
            g_LoRaManager.ack_pending = true;
            g_LoRaManager.ack_target_id = src;
            g_LoRaManager.ack_seq = seq;
            g_LoRaManager.ack_timestamp = Port_GetTick();
            LOG_PRINT("[MGR] ACK Pending...\r\n");
        }
        
        if (g_LoRaManager.cb_on_rx) {
            g_LoRaManager.cb_on_rx(&buf[i + header_len], payload_len, src, -50);
        }
    }
    g_LoRaManager.rx_len = 0;
}

// ============================================================
//                    4. 状态机
// ============================================================
void Manager_Run(void) {
    Drv_Run();
    
    if (g_LoRaManager.rx_len > 0) _Mgr_ProcessRx();
    
    // [核心] 处理挂起的 ACK
    if (g_LoRaManager.ack_pending) _Mgr_TrySendPendingACK();
    
    // 发送超时处理
    if (g_LoRaManager.state == MGR_STATE_WAIT_ACK) {
        if (Port_GetTick() - g_LoRaManager.state_tick > LORA_ACK_TIMEOUT_MS) {
            if (g_LoRaManager.retry_cnt < LORA_MAX_RETRY) {
                g_LoRaManager.retry_cnt++;
                LOG_PRINT("[MGR] Retry %d\r\n", g_LoRaManager.retry_cnt);
                Drv_AsyncSend(g_LoRaManager.tx_buf, g_LoRaManager.tx_len);
                g_LoRaManager.state_tick = Port_GetTick();
            } else {
                LOG_PRINT("[MGR] ACK Failed\r\n");
                _Mgr_ResetState();
                if (g_LoRaManager.cb_on_err) g_LoRaManager.cb_on_err(LORA_ERR_TIMEOUT);
            }
        }
    } else if (g_LoRaManager.state == MGR_STATE_TX_SENDING) {
        if (Drv_IsIdle()) {
            g_LoRaManager.state = MGR_STATE_IDLE;
            if (g_LoRaManager.cb_on_tx) g_LoRaManager.cb_on_tx(true);
        }
    }
}

static void _Mgr_TrySendPendingACK(void) {
    if (Port_GetTick() - g_LoRaManager.ack_timestamp < LORA_ACK_DELAY_MS) return;
    if (!Drv_IsIdle()) return;
    
    uint8_t ack_buf[32];
    uint16_t idx = 0;
    const LoRa_Config_t *cfg = Service_GetConfig();
    bool is_fixed = (cfg->tmode == LORA_TMODE_FIXED);
    
    if (is_fixed) {
        ack_buf[idx++] = (uint8_t)(g_LoRaManager.ack_target_id >> 8);
        ack_buf[idx++] = (uint8_t)(g_LoRaManager.ack_target_id & 0xFF);
        ack_buf[idx++] = cfg->channel;
    }
    
    ack_buf[idx++] = 'C'; ack_buf[idx++] = 'M'; ack_buf[idx++] = 0;
    uint8_t ctrl = 0x80;
    if (LORA_ENABLE_CRC) ctrl |= 0x20;
    ack_buf[idx++] = ctrl;
    ack_buf[idx++] = g_LoRaManager.ack_seq;
    ack_buf[idx++] = (uint8_t)(g_LoRaManager.ack_target_id & 0xFF);
    ack_buf[idx++] = (uint8_t)(g_LoRaManager.ack_target_id >> 8);
    ack_buf[idx++] = (uint8_t)(g_LoRaManager.local_id & 0xFF);
    ack_buf[idx++] = (uint8_t)(g_LoRaManager.local_id >> 8);
    
    if (LORA_ENABLE_CRC) {
        uint16_t crc_start = is_fixed ? 3 : 0;
        uint16_t crc = _CRC16_Calc(&ack_buf[crc_start], idx - crc_start);
        ack_buf[idx++] = (uint8_t)(crc & 0xFF);
        ack_buf[idx++] = (uint8_t)(crc >> 8);
    }
    
    LOG_HEX("[MGR] TX ACK", ack_buf, idx);
    if (Drv_AsyncSend(ack_buf, idx) == LORA_OK) {
        g_LoRaManager.ack_pending = false;
    }
}

static void _Mgr_ResetState(void) {
    g_LoRaManager.state = MGR_STATE_IDLE;
    g_LoRaManager.rx_len = 0;
    g_LoRaManager.ack_pending = false;
}

static uint16_t _CRC16_Calc(const uint8_t *data, uint16_t len) {
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

void Manager_RxCallback(uint8_t byte) {
    if (g_LoRaManager.rx_len < MGR_RX_BUF_SIZE) {
        g_LoRaManager.rx_buf[g_LoRaManager.rx_len++] = byte;
    }
}
