/**
  ******************************************************************************
  * @file    lora_driver_core.c
  * @author  LoRaPlat Team
  * @brief   Layer 2: 驱动核心逻辑 (通用 FSM 引擎)
  * @note    此文件包含通用的状态机逻辑，不包含任何具体硬件的 AT 指令。
  ******************************************************************************
  */

#include "lora_driver.h"
#include <string.h>

// ============================================================
//                    1. 内部配置
// ============================================================

#define DRV_TX_TIMEOUT_MS       3000    // 发送最大超时
#define DRV_AT_TIMEOUT_MS       500     // 单条 AT 指令超时
#define DRV_AT_RETRY_MAX        3       // AT 指令重试次数

// ============================================================
//                    2. 驱动控制块
// ============================================================

static struct {
    Drv_State_t state;          // 当前状态
    uint32_t    entry_tick;     // 状态进入时间戳
    Drv_Callback_t callback;    // 上层回调
    
    // --- AT 引擎上下文 ---
    uint8_t         at_idx;     // 当前任务索引
    uint8_t         at_retry;   // 当前重试次数
    bool            at_sent;    // 当前指令是否已发送
    
    // --- 接收缓冲区 (用于 AT 匹配) ---
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
    if (expect == NULL) return true; // 不需检查回复
    
    uint16_t len = g_LoRaPort.Phy_Read(s_Drv.rx_buf, sizeof(s_Drv.rx_buf) - 1);
    if (len > 0) {
        s_Drv.rx_buf[len] = '\0'; 
        if (strstr((char*)s_Drv.rx_buf, expect) != NULL) {
            return true;
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
    
    g_LoRaPort.Phy_StartTx(data, len);
    _FSM_SetState(DRV_STATE_TX_RUNNING);
    
    return LORA_OK;
}

LoRa_Result_t Drv_AsyncConfig(void) {
    if (s_Drv.state != DRV_STATE_IDLE) return LORA_ERR_BUSY;
    
    s_Drv.at_idx = 0;
    s_Drv.at_retry = 0;
    s_Drv.at_sent = false;
    
    g_LoRaPort.Phy_SetMode(true); // 进入配置模式
    _FSM_SetState(DRV_STATE_AT_PROCESS);
    
    return LORA_OK;
}

Drv_State_t Drv_GetState(void) {
    return s_Drv.state;
}

// ============================================================
//                    5. 驱动心跳 (FSM Engine)
// ============================================================

void Drv_Run(void) {
    uint32_t now = g_LoRaPort.GetTick();
    uint32_t elapsed = now - s_Drv.entry_tick;

    switch (s_Drv.state) {
        case DRV_STATE_IDLE:
            break;

        // --- 透传发送流程 ---
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

        // --- AT 配置流程 ---
        case DRV_STATE_AT_PROCESS:
            {
                const AT_Job_t *job = &g_LoRaConfigJobs[s_Drv.at_idx];
                
                // 1. 任务链结束
                if (job->cmd == NULL) {
                    g_LoRaPort.Phy_SetMode(false); // 退出配置模式
                    _FSM_Complete(LORA_OK);
                    return;
                }
                
                // 2. 发送指令
                if (!s_Drv.at_sent) {
                    g_LoRaPort.Phy_ClearRx();
                    g_LoRaPort.Phy_StartTx((uint8_t*)job->cmd, strlen(job->cmd));
                    s_Drv.at_sent = true;
                    s_Drv.entry_tick = now;
                    return;
                }
                
                // 3. 检查回复
                if (_Check_AT_Response(job->expect)) {
                    // 成功，进入额外延时（如果有）
                    // 这里简化处理：直接跳下一个，实际可增加一个 WAIT_DELAY 状态
                    s_Drv.at_idx++;
                    s_Drv.at_retry = 0;
                    s_Drv.at_sent = false;
                }
                // 4. 超时重试
                else if (elapsed > DRV_AT_TIMEOUT_MS) {
                    if (s_Drv.at_retry < DRV_AT_RETRY_MAX) {
                        s_Drv.at_retry++;
                        s_Drv.at_sent = false; // 触发重发
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
