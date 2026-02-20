#include "lora_service_monitor.h"
#include "lora_driver.h"
#include "lora_service.h"
#include "lora_osal.h"
#include "LoRaPlatConfig.h"

// 异常忙状态阈值 (10秒)
//#define LORA_MONITOR_BUSY_THRESHOLD_MS  10000，已移动至LoRaPlatConfig.h进行管理

static uint32_t s_BusyStartTime = 0;

void LoRa_Service_Monitor_Init(void) {
    s_BusyStartTime = 0;
}

/**
 * @brief 核心监视逻辑：检测驱动层是否陷入永久忙碌
 */
void LoRa_Service_Monitor_Run(void) {
    uint32_t now = OSAL_GetTick();

    // 检查驱动是否处于忙状态 (AUX高电平或DMA传输中)
    if (LoRa_Driver_IsBusy()) {
        if (s_BusyStartTime == 0) {
            s_BusyStartTime = now; // 开始计时
        } else {
            // 检查是否超过阈值
            if (now - s_BusyStartTime > LORA_MONITOR_BUSY_THRESHOLD_MS) {
                LORA_LOG("[MON] Critical Error: Driver stuck in BUSY for 10s!\r\n");
                
                // 触发自愈逻辑：重新初始化驱动
                // 这会尝试拉低 RST 引脚并重新发送 AT 指令配置模块
                const LoRa_Config_t *cfg = LoRa_Service_GetConfig();
                if (LoRa_Driver_Init(cfg)) {
                    LORA_LOG("[MON] Self-healing: Driver re-initialized.\r\n");
                } else {
                    LORA_LOG("[MON] Self-healing Failed: Hardware unresponsive.\r\n");
                }
                
                s_BusyStartTime = 0; // 重置计时器
            }
        }
    } else {
        // 只要有一次不忙，就重置计时器
        s_BusyStartTime = 0;
    }
}
