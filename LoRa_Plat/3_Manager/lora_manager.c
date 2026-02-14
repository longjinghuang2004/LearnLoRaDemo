#include "lora_manager.h"
#include "lora_driver.h"
#include "lora_port.h"
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

// 状态机状态
typedef enum {
    MGR_STATE_IDLE = 0,
    MGR_STATE_ACK_DELAY,    
    MGR_STATE_TX_RUNNING,   
    MGR_STATE_WAIT_ACK,     
} MgrState_t;

// 内部结构体定义 (私有化)
typedef struct {
    uint8_t TxBuffer[MGR_TX_BUF_SIZE];
    uint8_t RxBuffer[MGR_RX_BUF_SIZE];
    
    MgrState_t state;
    uint32_t   state_tick;  
    uint8_t    tx_seq;
    uint8_t    retry_cnt;
    uint16_t   rx_len;
    
    struct {
        uint16_t target_id;
        uint8_t  seq;
        bool     pending;
    } ack_pending;
    
    uint16_t   local_id;
    uint16_t   group_id;
    uint32_t   uuid;        
    
    OnRxData_t      cb_on_rx;
    OnTxResult_t    cb_on_tx;
    OnError_t       cb_on_err;
		
    bool     is_sending_ack; 
} LoRa_Manager_t;

// [修改] static
static LoRa_Manager_t s_Manager;
static uint16_t s_CurrentTxLen = 0;

// 引入全局配置 (用于读取 tmode, channel 等)
// 注意：Service 层已经提供了 GetConfig 接口，这里应该使用接口
#include "lora_service.h" 

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
    s_Manager.state = MGR_STATE_IDLE;
    s_Manager.ack_pending.pending = false;
}

static bool _PhySend(uint8_t *data, uint16_t len) {
    if (Drv_AsyncSend(data, len)) {
        s_Manager.state = MGR_STATE_TX_RUNNING;
        s_Manager.state_tick = OSAL_GetTick();
        return true;
    }
    return false;
}

static void _PrepareAck(uint16_t target_id, uint8_t seq) {
    s_Manager.ack_pending.target_id = target_id;
    s_Manager.ack_pending.seq = seq;
    s_Manager.ack_pending.pending = true;
    
    s_Manager.state = MGR_STATE_ACK_DELAY;
    s_Manager.state_tick = OSAL_GetTick();
}

static void _SendPendingAck(void) {
    uint8_t ack_buf[32];
    uint16_t idx = 0;
    uint16_t target_id = s_Manager.ack_pending.target_id;
    const LoRa_Config_t *cfg = LoRa_Service_GetConfig(); // [修改] 使用接口获取配置
    
    if (cfg->tmode == 1) {
        ack_buf[idx++] = (uint8_t)(target_id >> 8);
        ack_buf[idx++] = (uint8_t)(target_id & 0xFF);
        ack_buf[idx++] = cfg->channel;
    }

    ack_buf[idx++] = PROTOCOL_HEAD_0;
    ack_buf[idx++] = PROTOCOL_HEAD_1;
    ack_buf[idx++] = 0; 
    
    uint8_t ctrl = CTRL_MASK_TYPE; 
    if (LORA_ENABLE_CRC) ctrl |= CTRL_MASK_HAS_CRC;
    ack_buf[idx++] = ctrl;
    
    ack_buf[idx++] = s_Manager.ack_pending.seq; 
    
    ack_buf[idx++] = (uint8_t)(target_id & 0xFF);
    ack_buf[idx++] = (uint8_t)(target_id >> 8);
    ack_buf[idx++] = (uint8_t)(s_Manager.local_id & 0xFF);
    ack_buf[idx++] = (uint8_t)(s_Manager.local_id >> 8);
    
    if (LORA_ENABLE_CRC) {
        uint16_t crc_start = (cfg->tmode == 1) ? 3 : 0;
        uint16_t crc = CRC16_Calc(&ack_buf[crc_start + 2], idx - crc_start - 2);
        ack_buf[idx++] = (uint8_t)(crc & 0xFF);
        ack_buf[idx++] = (uint8_t)(crc >> 8);
    }
    
    ack_buf[idx++] = PROTOCOL_TAIL_0;
    ack_buf[idx++] = PROTOCOL_TAIL_1;
    
    LORA_HEXDUMP("[MGR] TX ACK", ack_buf, idx);
		
    s_Manager.is_sending_ack = true; 
		
    _PhySend(ack_buf, idx);
    
    s_Manager.ack_pending.pending = false;
}

static void Manager_ProcessRx(void) {
    uint16_t i = 0;
    uint16_t processed_len = 0;
    
    while (i + 11 <= s_Manager.rx_len) {
        if (s_Manager.RxBuffer[i] == PROTOCOL_HEAD_0 && 
            s_Manager.RxBuffer[i+1] == PROTOCOL_HEAD_1) {
            
            uint8_t p_len = s_Manager.RxBuffer[i+2];
            uint8_t ctrl = s_Manager.RxBuffer[i+3];
            bool has_crc = (ctrl & CTRL_MASK_HAS_CRC);
            uint16_t pkt_len = 9 + p_len + (has_crc ? 2 : 0) + 2;
            
            if (i + pkt_len > s_Manager.rx_len) break;
            
            if (s_Manager.RxBuffer[i + pkt_len - 2] == PROTOCOL_TAIL_0 && 
                s_Manager.RxBuffer[i + pkt_len - 1] == PROTOCOL_TAIL_1) {
                
                uint16_t target = (uint16_t)s_Manager.RxBuffer[i+5] | (s_Manager.RxBuffer[i+6] << 8);
                uint16_t src    = (uint16_t)s_Manager.RxBuffer[i+7] | (s_Manager.RxBuffer[i+8] << 8);
                uint8_t  seq    = s_Manager.RxBuffer[i+4];
                
                bool accept = (target == s_Manager.local_id) || 
                              (target == 0xFFFF) || 
                              (s_Manager.group_id != 0 && target == s_Manager.group_id);
                
                if (accept) {
                    bool crc_ok = true;
                    if (has_crc) {
                        uint16_t calc = CRC16_Calc(&s_Manager.RxBuffer[i+2], pkt_len - 6);
                        uint16_t recv = (uint16_t)s_Manager.RxBuffer[i + pkt_len - 4] | 
                                        (s_Manager.RxBuffer[i + pkt_len - 3] << 8);
                        if (calc != recv) crc_ok = false;
                    }
                    
                    if (crc_ok) {
                        if (ctrl & CTRL_MASK_TYPE) { // ACK
                            if (s_Manager.state == MGR_STATE_WAIT_ACK) {
                                LORA_LOG("[MGR] ACK Recv from %d\r\n", src);
                                if (s_Manager.cb_on_tx) s_Manager.cb_on_tx(true);
                                Manager_ResetState();
                            }
                        } else { // Data
                            if (s_Manager.cb_on_rx) {
                                s_Manager.cb_on_rx(&s_Manager.RxBuffer[i+9], p_len, src);
                            }
                            if ((ctrl & CTRL_MASK_NEED_ACK) && target != 0xFFFF) {
                                _PrepareAck(src, seq);
                            }
                        }
                    } else {
                        if (s_Manager.cb_on_err) s_Manager.cb_on_err(LORA_ERR_CRC_FAIL);
                    }
                }
                i += pkt_len;
                processed_len = i;
            } else { i++; processed_len = i; }
        } else { i++; processed_len = i; }
    }
    
    if (processed_len > 0) {
        if (processed_len < s_Manager.rx_len) {
            memmove(s_Manager.RxBuffer, &s_Manager.RxBuffer[processed_len], s_Manager.rx_len - processed_len);
            s_Manager.rx_len -= processed_len;
        } else { s_Manager.rx_len = 0; }
    }
}

// --- 核心接口 ---

void Manager_Init(OnRxData_t on_rx, OnTxResult_t on_tx, OnError_t on_err) {
    s_Manager.cb_on_rx = on_rx;
    s_Manager.cb_on_tx = on_tx;
    s_Manager.cb_on_err = on_err;
    Manager_ResetState();
}

void Manager_SetIdentity(uint16_t local_id, uint16_t group_id, uint32_t uuid) {
    s_Manager.local_id = local_id;
    s_Manager.group_id = group_id;
    s_Manager.uuid = uuid;
}

bool Manager_SendPacket(const uint8_t *payload, uint16_t len, uint16_t target_id) {
    if (s_Manager.state != MGR_STATE_IDLE) return false;
    
    uint8_t *p = s_Manager.TxBuffer;
    uint16_t idx = 0;
    const LoRa_Config_t *cfg = LoRa_Service_GetConfig(); // [修改] 使用接口获取配置
    
    if (cfg->tmode == 1) {
        p[idx++] = (uint8_t)(target_id >> 8);
        p[idx++] = (uint8_t)(target_id & 0xFF);
        p[idx++] = cfg->channel;
    }
    
    p[idx++] = PROTOCOL_HEAD_0;
    p[idx++] = PROTOCOL_HEAD_1;
    p[idx++] = (uint8_t)len;
    
    uint8_t ctrl = 0;
    if (LORA_ENABLE_ACK) ctrl |= CTRL_MASK_NEED_ACK;
    if (LORA_ENABLE_CRC) ctrl |= CTRL_MASK_HAS_CRC;
    p[idx++] = ctrl;
    
    p[idx++] = ++s_Manager.tx_seq;
    p[idx++] = (uint8_t)(target_id & 0xFF);
    p[idx++] = (uint8_t)(target_id >> 8);
    p[idx++] = (uint8_t)(s_Manager.local_id & 0xFF);
    p[idx++] = (uint8_t)(s_Manager.local_id >> 8);
    
    memcpy(&p[idx], payload, len);
    idx += len;
    
    if (LORA_ENABLE_CRC) {
        uint16_t crc_start = (cfg->tmode == 1) ? 3 : 0;
        uint16_t crc = CRC16_Calc(&p[crc_start + 2], idx - crc_start - 2);
        p[idx++] = (uint8_t)(crc & 0xFF);
        p[idx++] = (uint8_t)(crc >> 8);
    }
    
    p[idx++] = PROTOCOL_TAIL_0;
    p[idx++] = PROTOCOL_TAIL_1;
    
    s_CurrentTxLen = idx;
    LORA_HEXDUMP("[MGR] TX RAW", p, idx);
    
    s_Manager.is_sending_ack = false; 
		
    if (_PhySend(p, idx)) {
        s_Manager.retry_cnt = 0;
        return true;
    }
    return false;
}

void Manager_Run(void) {
    uint32_t now = OSAL_GetTick();
    const LoRa_Config_t *cfg = LoRa_Service_GetConfig(); // [修改] 使用接口获取配置
    
    // 1. RX 处理
    uint16_t read = Drv_Read(&s_Manager.RxBuffer[s_Manager.rx_len], MGR_RX_BUF_SIZE - s_Manager.rx_len);
    if (read > 0) {
        LORA_HEXDUMP("[MGR] RX RAW", &s_Manager.RxBuffer[s_Manager.rx_len], read);
        s_Manager.rx_len += read;
        Manager_ProcessRx();
    }
    
    // 2. 状态机
    switch (s_Manager.state) {
        case MGR_STATE_IDLE: break;
            
        case MGR_STATE_ACK_DELAY:
            if (now - s_Manager.state_tick > LORA_ACK_DELAY_MS) {
                if (!Drv_IsBusy()) {
                    _SendPendingAck(); 
                }
            }
            break;
            
        case MGR_STATE_TX_RUNNING:
            if (!Drv_IsBusy()) {
                if (s_Manager.is_sending_ack) {
                    Manager_ResetState();
                } 
                else {
                    uint8_t ctrl_offset = (cfg->tmode == 1) ? 6 : 3;
                    if (s_Manager.TxBuffer[ctrl_offset] & CTRL_MASK_NEED_ACK) {
                        s_Manager.state = MGR_STATE_WAIT_ACK;
                        s_Manager.state_tick = now;
                    } else {
                        if (s_Manager.cb_on_tx) s_Manager.cb_on_tx(true);
                        Manager_ResetState();
                    }
                }
            }
						
            else if (now - s_Manager.state_tick > LORA_TX_TIMEOUT_MS) {
                if (s_Manager.cb_on_err) s_Manager.cb_on_err(LORA_ERR_TX_TIMEOUT);
                Manager_ResetState();
            }
            break;
            
        case MGR_STATE_WAIT_ACK:
            if (now - s_Manager.state_tick > LORA_ACK_TIMEOUT_MS) {
                if (s_Manager.retry_cnt < LORA_MAX_RETRY) {
                    s_Manager.retry_cnt++;
                    LORA_LOG("[MGR] Retry %d\r\n", s_Manager.retry_cnt);
                    if (_PhySend(s_Manager.TxBuffer, s_CurrentTxLen)) {
                        // 重传成功启动
                    } else {
                        // 忙，下轮再试
                    }
                } else {
                    if (s_Manager.cb_on_err) s_Manager.cb_on_err(LORA_ERR_ACK_TIMEOUT);
                    if (s_Manager.cb_on_tx) s_Manager.cb_on_tx(false);
                    Manager_ResetState();
                }
            }
            break;
    }
}

bool Manager_IsBusy(void) {
    return (s_Manager.state != MGR_STATE_IDLE);
}
