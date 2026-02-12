#include "lora_driver.h"
#include "lora_port.h"
#include <string.h>

// ============================================================
//                    1. 内部定义
// ============================================================

// --- 内部 FSM 状态细分 ---
typedef enum {
    FSM_IDLE = 0,

    // --- 发送流程 (TX) ---
    FSM_TX_WAIT_AUX_BUSY,   // 发送后，等待 AUX 变高 (模块开始处理)
    FSM_TX_WAIT_AUX_IDLE,   // 等待 AUX 变低 (模块发送完毕)

    // --- 配置流程 (Config) - 简化版示意 ---
    FSM_CFG_ENTER_MODE,     // 拉高 MD0
    FSM_CFG_WAIT_STABLE,    // 等待模式切换稳定 (600ms)
    FSM_CFG_SEND_AT,        // 发送 AT 指令
    FSM_CFG_WAIT_RESP,      // 等待 AT 响应
    FSM_CFG_EXIT_MODE,      // 拉低 MD0
    FSM_CFG_WAIT_RECOVERY,  // 等待切回透传稳定

    // --- 自愈流程 (Reset) ---
    FSM_RESET_ASSERT,       // 拉低 RST
    FSM_RESET_DEASSERT,     // 拉高 RST
    FSM_RESET_WAIT_READY    // 等待模块启动
} Drv_InternalState_t;

// --- 超时阈值 (单位: ms) ---
#define TIMEOUT_TX_START        1000    // 发送后 AUX 没反应
#define TIMEOUT_TX_COMPLETE     3000    // 空中发送太久
#define TIMEOUT_RESET_HOLD      10      // 复位拉低保持时间
#define TIMEOUT_RESET_RECOVERY  1000    // 复位后启动时间
#define TIMEOUT_MODE_SWITCH     600     // 模式切换稳定时间

// --- 驱动控制块 ---
static struct {
    Drv_InternalState_t state;      // 当前内部状态
    uint32_t entry_tick;            // 进入当前状态的时间戳
    Drv_Callback_t callback;        // 上层回调
    
    // 缓存配置请求 (用于 Config 流程)
    LoRa_Config_t pending_cfg;
} s_Drv;

// ============================================================
//                    2. 内部辅助函数
// ============================================================

// 切换状态并记录时间戳
static void _FSM_SetState(Drv_InternalState_t next) {
    s_Drv.state = next;
    s_Drv.entry_tick = Port_GetTick();
    // LORA_LOG("FSM: -> %d", next); // 调试时可开启
}

// 报告错误并触发自愈
static void _FSM_ReportError(LoRa_Result_t err) {
    LORA_LOG("Error: Code %d. Resetting...", err);
    
    // 1. 通知上层
    if (s_Drv.callback) {
        s_Drv.callback(err);
    }
    
    // 2. 强制跳转到复位流程
    _FSM_SetState(FSM_RESET_ASSERT);
}

// ============================================================
//                    3. 接口实现
// ============================================================

void Drv_Init(Drv_Callback_t cb) {
    Port_Init();
    s_Drv.callback = cb;
    
    // 上电直接进入复位流程，确保模块状态已知
    LORA_LOG("Drv Init. Resetting module...");
    _FSM_SetState(FSM_RESET_ASSERT);
}

LoRa_Result_t Drv_AsyncSend(const uint8_t *data, uint16_t len) {
    // 1. 状态守卫
    if (s_Drv.state != FSM_IDLE) {
        return LORA_ERR_BUSY;
    }
    if (len == 0 || len > 512) {
        return LORA_ERR_PARAM;
    }

    // 2. 硬件操作 (非阻塞)
    Port_WriteData(data, len);

    // 3. 状态跳转
    // 发送数据后，模块需要一点时间反应才会拉高 AUX
    _FSM_SetState(FSM_TX_WAIT_AUX_BUSY);
    
    return LORA_OK;
}

LoRa_Result_t Drv_AsyncConfig(const LoRa_Config_t *cfg) {
    if (s_Drv.state != FSM_IDLE) return LORA_ERR_BUSY;
    
    // 缓存配置
    memcpy(&s_Drv.pending_cfg, cfg, sizeof(LoRa_Config_t));
    
    // 启动配置流程
    _FSM_SetState(FSM_CFG_ENTER_MODE);
    return LORA_OK;
}

Drv_State_t Drv_GetState(void) {
    if (s_Drv.state == FSM_IDLE) return DRV_STATE_IDLE;
    if (s_Drv.state >= FSM_RESET_ASSERT) return DRV_STATE_RESET;
    if (s_Drv.state >= FSM_CFG_ENTER_MODE && s_Drv.state <= FSM_CFG_WAIT_RECOVERY) return DRV_STATE_CONFIG;
    return DRV_STATE_TX;
}

// ============================================================
//                    4. 核心状态机 (Heartbeat)
// ============================================================

void Drv_Run(void) {
    // 极速退出检查
    if (s_Drv.state == FSM_IDLE) return;

    uint32_t now = Port_GetTick();
    uint32_t elapsed = now - s_Drv.entry_tick;

    switch (s_Drv.state) {
        
        // ----------------------------------------------------
        // TX 流程: Write -> Wait AUX High -> Wait AUX Low
        // ----------------------------------------------------
        case FSM_TX_WAIT_AUX_BUSY:
            // 模块拉高 AUX，说明开始处理数据
            if (Port_GetAUX()) {
                _FSM_SetState(FSM_TX_WAIT_AUX_IDLE);
            }
            // 超时守卫: 模块没反应 (可能死机或波特率不对)
            else if (elapsed > TIMEOUT_TX_START) {
                _FSM_ReportError(LORA_ERR_TX_TIMEOUT);
            }
            break;

        case FSM_TX_WAIT_AUX_IDLE:
            // 模块拉低 AUX，说明发送完成
            if (!Port_GetAUX()) {
                _FSM_SetState(FSM_IDLE);
                if (s_Drv.callback) s_Drv.callback(LORA_OK); // 成功回调
            }
            // 超时守卫: 发送卡死
            else if (elapsed > TIMEOUT_TX_COMPLETE) {
                _FSM_ReportError(LORA_ERR_TX_TIMEOUT);
            }
            break;

        // ----------------------------------------------------
        // Config 流程 (简化版: 仅演示模式切换)
        // ----------------------------------------------------
        case FSM_CFG_ENTER_MODE:
            Port_SetMD0(true); // 进入配置模式
            _FSM_SetState(FSM_CFG_WAIT_STABLE);
            break;
            
        case FSM_CFG_WAIT_STABLE:
            if (elapsed > TIMEOUT_MODE_SWITCH) {
                // 这里应该发送 AT 指令，为简化 Phase 1，直接跳到退出
                // TODO: 实现 AT 指令发送状态机
                _FSM_SetState(FSM_CFG_EXIT_MODE);
            }
            break;
            
        case FSM_CFG_EXIT_MODE:
            Port_SetMD0(false); // 回到透传模式
            _FSM_SetState(FSM_CFG_WAIT_RECOVERY);
            break;
            
        case FSM_CFG_WAIT_RECOVERY:
            if (elapsed > TIMEOUT_MODE_SWITCH) {
                // 等待 AUX 变低确保空闲
                if (!Port_GetAUX()) {
                    _FSM_SetState(FSM_IDLE);
                    if (s_Drv.callback) s_Drv.callback(LORA_OK);
                }
            }
            break;

        // ----------------------------------------------------
        // Reset 自愈流程
        // ----------------------------------------------------
        case FSM_RESET_ASSERT:
            Port_SetRST(false); // 拉低复位引脚
            if (elapsed > TIMEOUT_RESET_HOLD) {
                _FSM_SetState(FSM_RESET_DEASSERT);
            }
            break;

        case FSM_RESET_DEASSERT:
            Port_SetRST(true); // 释放复位
            _FSM_SetState(FSM_RESET_WAIT_READY);
            break;

        case FSM_RESET_WAIT_READY:
            // 等待模块启动完成 (AUX 变低)
            if (elapsed > TIMEOUT_RESET_RECOVERY) {
                if (!Port_GetAUX()) {
                    LORA_LOG("Reset Complete. Module Ready.");
                    _FSM_SetState(FSM_IDLE);
                    // 注意：复位成功通常不回调 OK，除非是 Init 触发的
                } else {
                    // 复位后 AUX 还是高？硬件彻底挂了
                    LORA_LOG("Fatal: Module stuck after reset!");
                    // 可以在这里进入一个死锁状态 DRV_STATE_ERROR
                }
            }
            break;

        default: break;
    }
}
