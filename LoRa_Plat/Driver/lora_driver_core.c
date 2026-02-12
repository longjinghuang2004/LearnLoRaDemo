#include "lora_driver.h"
#include "lora_port.h" 
#include <string.h>
#include <stdio.h> // for sprintf

// ============================================================
//                    1. 内部配置与常量
// ============================================================

#define DRV_TX_TIMEOUT_MS       3000    
#define DRV_AT_TIMEOUT_MS       500     
#define DRV_AT_RETRY_MAX        3       

static const AT_Job_t g_ConfigJobs[] = {
    {"AT\r\n",              "OK", 50},  
    {"AT+TMODE=1\r\n",      "OK", 50},  
    {"AT+WLRATE=23,5\r\n",  "OK", 50},  
    {NULL, NULL, 0}                     
};

// ============================================================
//                    2. 驱动控制块
// ============================================================

static struct {
    Drv_State_t state;          
    uint32_t    entry_tick;     
    Drv_Callback_t callback;    
    
    const uint8_t *tx_data;
    uint16_t       tx_len;
    
    const AT_Job_t *at_jobs;    
    uint8_t         at_idx;     
    uint8_t         at_retry;   
    bool            at_sent;    
    
    uint8_t rx_buf[128];
    
} s_Drv;

// ============================================================
//                    3. 内部辅助函数
// ============================================================

static void _FSM_SetState(Drv_State_t next) {
    s_Drv.state = next;
    s_Drv.entry_tick = g_LoRaPort.GetTick();
}

static void _FSM_Complete(LoRa_Result_t res) {
    _FSM_SetState(DRV_STATE_IDLE);
    if (s_Drv.callback) {
        s_Drv.callback(res);
    }
}

static bool _Check_AT_Response(const char *expect) {
    uint16_t len = g_LoRaPort.Phy_Read(s_Drv.rx_buf, sizeof(s_Drv.rx_buf) - 1);
    if (len > 0) {
        s_Drv.rx_buf[len] = '\0'; 
        if (strstr((char*)s_Drv.rx_buf, expect)) {
            return true;
        }
    }
    return false;
}

// [新增] 阻塞式 AT 发送 (仅用于初始化/配置阶段)
static bool _SendAT_Blocking(const char *cmd, const char *expect, uint32_t timeout) {
    g_LoRaPort.Phy_ClearRx();
    g_LoRaPort.Phy_StartTx((uint8_t*)cmd, strlen(cmd));
    
    uint32_t start = g_LoRaPort.GetTick();
    uint8_t rx_buf[128];
    uint16_t rx_idx = 0;
    
    while (g_LoRaPort.GetTick() - start < timeout) {
        uint8_t byte;
        if (g_LoRaPort.Phy_Read(&byte, 1) > 0) {
            rx_buf[rx_idx++] = byte;
            if (rx_idx >= 127) rx_idx = 127;
            rx_buf[rx_idx] = '\0';
            if (strstr((char*)rx_buf, expect)) return true;
        }
    }
    return false;
}

// ============================================================
//                    4. 核心接口实现
// ============================================================

void Drv_Init(Drv_Callback_t cb) {
    s_Drv.callback = cb;
    if (g_LoRaPort.Init) g_LoRaPort.Init();
    _FSM_SetState(DRV_STATE_IDLE);
    if (g_LoRaPort.Phy_HardReset) g_LoRaPort.Phy_HardReset();
}

LoRa_Result_t Drv_AsyncSend(const uint8_t *data, uint16_t len) {
    if (s_Drv.state != DRV_STATE_IDLE) return LORA_ERR_BUSY;
    if (len == 0) return LORA_ERR_PARAM;
    
    s_Drv.tx_data = data;
    s_Drv.tx_len = len;
    
    g_LoRaPort.Phy_StartTx(data, len);
    _FSM_SetState(DRV_STATE_TX_RUNNING);
    
    return LORA_OK;
}

LoRa_Result_t Drv_AsyncConfig(void) {
    if (s_Drv.state != DRV_STATE_IDLE) return LORA_ERR_BUSY;
    
    s_Drv.at_jobs = g_ConfigJobs; 
    s_Drv.at_idx = 0;
    s_Drv.at_retry = 0;
    s_Drv.at_sent = false;
    
    g_LoRaPort.Phy_SetMode(true);
    _FSM_SetState(DRV_STATE_AT_PROCESS);
    
    return LORA_OK;
}

Drv_State_t Drv_GetState(void) {
    return s_Drv.state;
}

bool Drv_IsIdle(void) {
    return (s_Drv.state == DRV_STATE_IDLE);
}

// [修复] 补回阻塞式配置函数
bool Drv_ApplyConfig(const LoRa_Config_t *cfg) {
    g_LoRaPort.Phy_SetMode(true);
    // 简单延时，等待模块进入配置模式 (这里可以用阻塞，因为是初始化阶段)
    uint32_t start = g_LoRaPort.GetTick();
    while(g_LoRaPort.GetTick() - start < 100); 
    
    char cmd[64];
    bool success = true;

    sprintf(cmd, "AT+ADDR=%02X,%02X\r\n", (cfg->hw_addr >> 8) & 0xFF, cfg->hw_addr & 0xFF);
    if (!_SendAT_Blocking(cmd, "OK", 500)) success = false;

    sprintf(cmd, "AT+WLRATE=%d,%d\r\n", cfg->channel, cfg->air_rate);
    if (!_SendAT_Blocking(cmd, "OK", 500)) success = false;

    sprintf(cmd, "AT+TPOWER=%d\r\n", cfg->power);
    if (!_SendAT_Blocking(cmd, "OK", 500)) success = false;

    sprintf(cmd, "AT+TMODE=%d\r\n", cfg->tmode);
    if (!_SendAT_Blocking(cmd, "OK", 500)) success = false;

    g_LoRaPort.Phy_SetMode(false);
    start = g_LoRaPort.GetTick();
    while(g_LoRaPort.GetTick() - start < 100); 
    
    return success;
}

// [修复] 补回智能配置函数
bool Drv_SmartConfig(void) {
    g_LoRaPort.Phy_SetMode(true);
    uint32_t start = g_LoRaPort.GetTick();
    while(g_LoRaPort.GetTick() - start < 500); 
    
    if (_SendAT_Blocking("AT\r\n", "OK", 200)) {
        // 恢复默认
        _SendAT_Blocking("AT+TMODE=0\r\n", "OK", 500);
        _SendAT_Blocking("AT+ADDR=00,00\r\n", "OK", 500);
        g_LoRaPort.Phy_SetMode(false);
        return true;
    }
    g_LoRaPort.Phy_SetMode(false);
    return false;
}

// ============================================================
//                    5. 驱动心跳
// ============================================================

void Drv_Run(void) {
    uint32_t now = g_LoRaPort.GetTick();
    uint32_t elapsed = now - s_Drv.entry_tick;

    switch (s_Drv.state) {
        case DRV_STATE_IDLE:
            break;

        case DRV_STATE_TX_RUNNING:
            if (!g_LoRaPort.Phy_IsBusy()) {
                _FSM_SetState(DRV_STATE_TX_RECOVERY);
            }
            else if (elapsed > DRV_TX_TIMEOUT_MS) {
                _FSM_Complete(LORA_ERR_TIMEOUT); 
            }
            break;

        case DRV_STATE_TX_RECOVERY:
            if (elapsed >= g_LoRaPort.Phy_GetRecoveryTime()) {
                _FSM_Complete(LORA_OK); 
            }
            break;

        case DRV_STATE_AT_PROCESS:
            {
                const AT_Job_t *job = &s_Drv.at_jobs[s_Drv.at_idx];
                
                if (job->cmd == NULL) {
                    g_LoRaPort.Phy_SetMode(false); 
                    _FSM_Complete(LORA_OK);
                    return;
                }
                
                if (!s_Drv.at_sent) {
                    g_LoRaPort.Phy_ClearRx(); 
                    g_LoRaPort.Phy_StartTx((uint8_t*)job->cmd, strlen(job->cmd));
                    s_Drv.at_sent = true;
                    s_Drv.entry_tick = now; 
                    return;
                }
                
                if (_Check_AT_Response(job->expect)) {
                    s_Drv.at_idx++;
                    s_Drv.at_retry = 0;
                    s_Drv.at_sent = false;
                }
                else if (elapsed > DRV_AT_TIMEOUT_MS) {
                    if (s_Drv.at_retry < DRV_AT_RETRY_MAX) {
                        s_Drv.at_retry++;
                        s_Drv.at_sent = false; 
                    } else {
                        g_LoRaPort.Phy_SetMode(false);
                        _FSM_Complete(LORA_ERR_AT_FAIL);
                    }
                }
            }
            break;
            
        default: break;
    }
}
