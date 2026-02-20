/**
  ******************************************************************************
  * @file    lora_service.c
  * @author  LoRaPlat Team
  * @brief   LoRa 业务服务层实现 (V3.9.4 Decoupled)
  *          集成 OTA 拦截、内部自举软重启、动态 ACK 路由。
  *          已适配 Manager 层的回调机制，不再直接依赖 FSM。
  ******************************************************************************
  */

#include "lora_service.h"
#include "lora_manager.h"
#include "lora_service_config.h"
#include "lora_service_monitor.h"
#include "lora_service_command.h"
#include "lora_port.h"   // 引入 Port 层接口
#include "lora_driver.h" // 引入 Driver 层接口
#include "lora_osal.h"
#include <string.h>

// ============================================================
//                    内部变量与状态
// ============================================================

// 服务层状态机
typedef enum {
    SVC_STATE_RUNNING = 0,
    SVC_STATE_REBOOT_WAIT, // 等待倒计时结束 (OTA 回执发送中)
    SVC_STATE_REBOOT_NOW   // 立即重启 (在主循环安全点执行)
} Service_State_t;

static struct {
    Service_State_t state;
    uint32_t reboot_tick;
} s_SvcCtx;

// 保存初始化参数，用于自举重启
static const LoRa_Callback_t *s_AppCb = NULL;
static uint16_t s_SavedNetID = 0;

// 保存 Cipher 指针，用于重启后恢复
static const LoRa_Cipher_t *s_SavedCipher = NULL;

// ============================================================
//                    内部回调 (Manager -> Service)
// ============================================================

/**
 * @brief 接收数据回调 (由 Manager 层调用)
 */
static void _Service_OnRecv(uint8_t *data, uint16_t len, uint16_t src_id) {
    
#if (defined(LORA_ENABLE_OTA_CFG) && LORA_ENABLE_OTA_CFG == 1)
    // 1. 尝试拦截 OTA 指令 (CMD:...)
    if (len > 4 && memcmp(data, "CMD:", 4) == 0) {
        // [优化] 将栈变量改为静态变量，避免栈溢出 (合计约 200 字节)
        // 注意：这使得该函数不可重入，但在裸机环境下是安全的
        static char s_CmdCopyBuf[128];
        static char s_RespBuf[64];
        
        uint16_t copy_len = (len < 127) ? len : 127;
        memcpy(s_CmdCopyBuf, data, copy_len);
        s_CmdCopyBuf[copy_len] = '\0';
        
        // 调用 Command 模块处理
        if (LoRa_Service_Command_Process(s_CmdCopyBuf, s_RespBuf, sizeof(s_RespBuf))) {
            // 发送回执 (使用 Confirmed 确保主机收到)
            LoRa_Service_Send((uint8_t*)s_RespBuf, strlen(s_RespBuf), src_id, LORA_OPT_CONFIRMED);
        }
        return; // 拦截成功，不透传给 App
    }
#endif

    // 2. 正常业务数据透传
    if (s_AppCb && s_AppCb->OnRecvData) {
        // 简单的 RSSI 模拟 (实际应从驱动获取)
        LoRa_RxMeta_t meta = { .rssi = -60, .snr = 10 };
        s_AppCb->OnRecvData(src_id, data, len, &meta);
    }
    
    // 触发事件
    if (s_AppCb && s_AppCb->OnEvent) {
        s_AppCb->OnEvent(LORA_EVENT_MSG_RECEIVED, NULL);
    }
}

/**
 * @brief 发送结果回调 (由 Manager 层调用)
 * @note  替代了原有的 FSM -> Service 直接 Notify 机制
 */
static void _Service_OnTxResult(LoRa_MsgID_t msg_id, bool success) {
    if (success) {
        // 对应原 LORA_EVENT_TX_SUCCESS_ID
        if (s_AppCb && s_AppCb->OnEvent) {
            s_AppCb->OnEvent(LORA_EVENT_TX_SUCCESS_ID, &msg_id);
        }
    } else {
        // 对应原 LORA_EVENT_TX_FAILED_ID
        if (s_AppCb && s_AppCb->OnEvent) {
            s_AppCb->OnEvent(LORA_EVENT_TX_FAILED_ID, &msg_id);
        }
    }
}

// ============================================================
//                    私有函数：内部自举
// ============================================================

/**
 * @brief 执行真正的重初始化流程 (软重启核心)
 * @note  此函数包含耗时操作 (Flash读取, AT握手)，必须在主循环上下文中调用
 */
static void _Service_DoReinit(void) {
    LORA_LOG("[SVC] Performing Soft Reboot...\r\n");
    
    // 1. 初始化配置模块 (加载代码中的默认宏定义)
    // 此时内存中的配置是 DEFAULT_LORA_xxx
    LoRa_Service_Config_Init();
    
    // 2. 尝试从 Flash/NVS 加载配置
    if (s_AppCb && s_AppCb->LoadConfig) {
        LoRa_Config_t flash_cfg;
        s_AppCb->LoadConfig(&flash_cfg);
        
        // 校验 Flash 数据是否合法
        if (flash_cfg.magic == LORA_CFG_MAGIC) {
            // A. 合法：应用到内存
            LoRa_Service_Config_Set(&flash_cfg);
            LORA_LOG("[SVC] Config Loaded from Flash.\r\n");
        } else {
            // B. 非法：Flash 损坏或为空
            LORA_LOG("[SVC] Flash Invalid! Restoring Defaults...\r\n");
            
            // [新增] 自动修复逻辑：
            // 此时内存中已经是默认值 (由步骤1 Init 完成)
            // 我们只需要触发保存事件，将默认值写回 Flash
            const LoRa_Config_t *default_cfg = LoRa_Service_Config_Get();
            
            // 通知 App 层写入 Flash
            if (s_AppCb->SaveConfig) {
                s_AppCb->SaveConfig(default_cfg);
                LORA_LOG("[SVC] Defaults saved to Flash.\r\n");
            }
        }
    }
    
    // 3. 应用运行时 NetID 覆盖 (优先级最高)
    // 如果 Init 时传入了非 0 的 ID，强制使用该 ID，覆盖 Flash 中的设置
    if (s_SavedNetID != 0) {
        // 获取当前配置副本
        LoRa_Config_t temp_cfg = *LoRa_Service_Config_Get();
        // 修改 NetID
        temp_cfg.net_id = s_SavedNetID;
        // 写回
        LoRa_Service_Config_Set(&temp_cfg);
        LORA_LOG("[SVC] NetID Overridden: %d\r\n", s_SavedNetID);
    }
    
    // 获取最终确定的配置指针
    const LoRa_Config_t *cfg = LoRa_Service_Config_Get();
    
    // 4. 初始化驱动 (发送 AT 指令配置硬件)
    // 注意：这是阻塞操作，耗时约 1-2 秒
    if (!LoRa_Driver_Init(cfg)) {
        LORA_LOG("[SVC] Driver Init Failed! Check Hardware.\r\n");
    }
    
    // 5. 初始化管理器 (逻辑层)
    // 构造回调结构体并注入 Manager
    LoRa_Manager_Callback_t mgr_cb = {
        .OnRecv = _Service_OnRecv,
        .OnTxResult = _Service_OnTxResult
    };
    
    LoRa_Manager_Init(cfg, &mgr_cb);
    
    // 6. 恢复加密算法注册 (如果之前注册过)
    if (s_SavedCipher) {
        LoRa_Manager_RegisterCipher(s_SavedCipher);
    }       
    
    // 7. 初始化监视器
    LoRa_Service_Monitor_Init();
    
    // 8. 恢复服务状态
    s_SvcCtx.state = SVC_STATE_RUNNING;
    
    // 9. 通知用户初始化完成
    if (s_AppCb && s_AppCb->OnEvent) {
        s_AppCb->OnEvent(LORA_EVENT_INIT_SUCCESS, NULL);
    }
}

// ============================================================
//                    核心接口实现
// ============================================================

void LoRa_Service_Init(const LoRa_Callback_t *callbacks, uint16_t override_net_id){
    s_AppCb = callbacks;
    s_SavedNetID = override_net_id;
    
    // 首次初始化，直接执行
    _Service_DoReinit();
}

void LoRa_Service_Run(void) {
    // [核心] 检查是否需要执行软重启
    // 在主循环开头执行，确保没有深层调用栈
    if (s_SvcCtx.state == SVC_STATE_REBOOT_NOW) {
        _Service_DoReinit();
        return; // 重启后直接返回，开始新的一轮循环
    }

    // 1. 协议栈轮询
    LoRa_Manager_Run();
    LoRa_Service_Monitor_Run();
    
    // 2. 软重启倒计时逻辑 (OTA 场景)
    if (s_SvcCtx.state == SVC_STATE_REBOOT_WAIT) {
        if (OSAL_GetTick() - s_SvcCtx.reboot_tick > LORA_REBOOT_DELAY_MS) {
            // 倒计时结束，切换状态，下一轮循环执行
            s_SvcCtx.state = SVC_STATE_REBOOT_NOW;
        }
    }
}

LoRa_MsgID_t LoRa_Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id, LoRa_SendOpt_t opt) {
    LORA_CHECK(data && len > 0, 0);
    // 透传给 Manager 层
    return LoRa_Manager_Send(data, len, target_id, opt);
}

void LoRa_Service_SoftReset(void) {
    // 外部请求重启，设置标志位，异步执行
    s_SvcCtx.state = SVC_STATE_REBOOT_NOW;
}

uint32_t LoRa_Service_GetSleepDuration(void) {
    return LoRa_Manager_GetSleepDuration();
}

void LoRa_Service_FactoryReset(void) {
    LoRa_Service_Config_FactoryReset();
    if (s_AppCb && s_AppCb->OnEvent) {
        s_AppCb->OnEvent(LORA_EVENT_FACTORY_RESET, NULL);
    }
    // 触发重启请求
    LoRa_Service_NotifyEvent(LORA_EVENT_REBOOT_REQ, NULL);
}

const LoRa_Config_t* LoRa_Service_GetConfig(void) {
    return LoRa_Service_Config_Get();
}

void LoRa_Service_SetConfig(const LoRa_Config_t *cfg) {
    LoRa_Service_Config_Set(cfg);
}

void LoRa_Service_NotifyEvent(LoRa_Event_t event, void *arg) {
    // --- 内部拦截处理 ---
    
    if (event == LORA_EVENT_CONFIG_COMMIT) {
        // 收到配置提交请求，调用回调保存到 Flash
        if (s_AppCb && s_AppCb->SaveConfig) {
            s_AppCb->SaveConfig((const LoRa_Config_t*)arg);
        }
    }
    else if (event == LORA_EVENT_REBOOT_REQ) {
        // 收到重启请求，进入倒计时
        s_SvcCtx.state = SVC_STATE_REBOOT_WAIT;
        s_SvcCtx.reboot_tick = OSAL_GetTick();
        // 不透传给 App，Service 内部处理
        return; 
    }

    // [注意] TX_SUCCESS/FAILED 事件现在由 _Service_OnTxResult 处理，此处不再拦截

    // --- 透传给 App ---
    if (s_AppCb && s_AppCb->OnEvent) {
        s_AppCb->OnEvent(event, arg);
    }
}

// [修改] 现有的 IsBusy 保持不变，仅检查业务逻辑
bool LoRa_Service_IsBusy(void) {
    return LoRa_Manager_IsBusy();
}

// [新增] CanSleep 实现
bool LoRa_Service_CanSleep(void) {
    // 1. 检查业务逻辑层 (Manager FSM, Queues)
    if (LoRa_Manager_IsBusy()) return false;

    // 2. 检查物理驱动层 (Driver AUX pin)
    // 如果模组正在空中发送或接收，AUX 会拉高，此时不能睡
    if (LoRa_Driver_IsBusy()) return false;

    // 3. 检查 Port 层硬件事件 (Interrupt Flags)
    // 如果刚才 Run() 之后又来了中断（如收到新数据），这里返回 true，阻止休眠
    // CheckAndClear 会清除标志，确保下一次循环能正确判断
    if (LoRa_Port_CheckAndClearHwEvent()) return false;

    return true;
}
void LoRa_Service_RegisterCipher(const LoRa_Cipher_t *cipher) {
    s_SavedCipher = cipher;
    LoRa_Manager_RegisterCipher(cipher);
}
