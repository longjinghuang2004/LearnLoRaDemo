#ifndef __LORA_SERVICE_H
#define __LORA_SERVICE_H

#include "LoRaPlatConfig.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 事件定义
// ============================================================
typedef enum {
    LORA_EVENT_INIT_SUCCESS = 0,
    LORA_EVENT_BIND_SUCCESS,    // 绑定新ID成功
    LORA_EVENT_CONFIG_START,    // 进入配置模式
    LORA_EVENT_CONFIG_COMMIT,   // 配置提交
    LORA_EVENT_FACTORY_RESET,   // 恢复出厂
    LORA_EVENT_REBOOT_REQ,      // 请求重启 (例如绑定后)
    LORA_EVENT_MSG_RECEIVED,    // 收到普通消息 (用于闪灯等UI指示)
    LORA_EVENT_MSG_SENT         // 发送完成
} LoRa_Event_t;

// ============================================================
//                    2. 抽象接口定义 (回调函数)
// ============================================================
typedef struct {
    // --- 存储接口 (必须实现) ---
    // 保存配置到非易失性存储器 (Flash/EEPROM)
    void (*SaveConfig)(const LoRa_Config_t *cfg);
    // 从非易失性存储器读取配置
    void (*LoadConfig)(LoRa_Config_t *cfg);
    
    // --- 硬件能力接口 (必须实现) ---
    // 获取毫秒级系统时间戳
    uint32_t (*GetTick)(void);
    // 获取随机数种子 (用于生成UUID)
    uint32_t (*GetRandomSeed)(void);
    // 硬件复位 (可选，若不实现则无法自动重启)
    void (*SystemReset)(void);
    
    // --- 业务接口 (可选) ---
    // 收到应用层数据
    void (*OnRecvData)(uint16_t src_id, const uint8_t *data, uint16_t len);
    // 系统事件通知 (用于LED指示、日志打印等)
    void (*OnEvent)(LoRa_Event_t event, void *arg);
    
} LoRa_Callback_t;

// ============================================================
//                    3. 全局变量与函数
// ============================================================

// 全局配置对象 (供外部读取状态)
extern LoRa_Config_t g_LoRaConfig_Current;

/**
  * @brief  初始化 LoRa 服务层
  * @param  callbacks: 外部实现的接口结构体指针
  * @param  override_net_id: 强制覆盖 NetID (调试用，0表示不覆盖)
  */
void LoRa_Service_Init(const LoRa_Callback_t *callbacks, uint16_t override_net_id);

/**
  * @brief  服务层主循环 (需在 main loop 中调用)
  */
void LoRa_Service_Run(void);

/**
  * @brief  发送数据 (封装 Manager 接口)
  */
bool LoRa_Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id);

/**
  * @brief  执行工厂重置 (清除配置并恢复模块)
  */
void LoRa_Service_FactoryReset(void);

#endif // __LORA_SERVICE_H
