/**
  ******************************************************************************
  * @file    lora_service.c
  * @author  LoRaPlat Team
  * @brief   LoRa 业务服务层实现 (V3.9.2)
  *          集成 OTA 拦截、内部自举软重启、动态 ACK 路由。
  ******************************************************************************
  */

#include "lora_service.h"
#include "lora_manager.h"
#include "lora_service_config.h"
#include "lora_service_monitor.h"
#include "lora_service_command.h"
#include "lora_driver.h"
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


// ============================================================
//                    私有函数：内部自举
// ============================================================

// [新增] 前向声明，解决 _Service_DoReinit 中的调用依赖
static void _Service_OnRecv(uint8_t *data, uint16_t len, uint16_t src_id);


/**
 * @brief 执行真正的重初始化流程
 * @note  此函数包含耗时操作 (Flash读取, AT握手)，必须在主循环上下文中调用
 */
static void _Service_DoReinit(void) {
    LORA_LOG("[SVC] Performing Soft Reboot...\r\n");
    
    // 1. 重新读取配置 (Config 模块会通过回调从 Flash 加载)
    LoRa_Service_Config_Init();
    
    // [可选] 再次应用 NetID 覆盖 (如果需要)
    if (s_SavedNetID != 0) {
        LoRa_Config_t temp_cfg = *LoRa_Service_Config_Get();
        temp_cfg.net_id = s_SavedNetID;
        LoRa_Service_Config_Set(&temp_cfg);
    }
    
    const LoRa_Config_t *cfg = LoRa_Service_Config_Get();
    
    // 2. 重新初始化驱动 (阻塞式 AT 握手)
    if (!LoRa_Driver_Init(cfg)) {
        LORA_LOG("[SVC] Driver Init Failed! Check Hardware.\r\n");
    }
    
    // 3. 重新初始化管理器
    // 注意：这里重新注册了接收回调
    // 必须前向声明 _Service_OnRecv 或将其放在前面
    
    LoRa_Manager_Init(cfg, _Service_OnRecv);
    
    // 4. 重新初始化监视器
    LoRa_Service_Monitor_Init();
    
    // 5. 恢复状态
    s_SvcCtx.state = SVC_STATE_RUNNING;
    
    // 6. 通知用户
    if (s_AppCb && s_AppCb->OnEvent) {
        s_AppCb->OnEvent(LORA_EVENT_INIT_SUCCESS, NULL);
    }
}

// ============================================================
//                    内部回调 (Manager -> Service)
// ============================================================

// 接收数据回调
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

// ============================================================
//                    核心接口实现
// ============================================================

void LoRa_Service_Init(const LoRa_Callback_t *callbacks, uint16_t override_net_id) {
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

bool LoRa_Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id, LoRa_SendOpt_t opt) {
    LORA_CHECK(data && len > 0, false);
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

    // --- 透传给 App ---
    if (s_AppCb && s_AppCb->OnEvent) {
        s_AppCb->OnEvent(event, arg);
    }
}
