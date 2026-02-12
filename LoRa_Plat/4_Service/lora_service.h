/**
  ******************************************************************************
  * @file    lora_service.h
  * @author  LoRaPlat Team
  * @brief   Layer 4: 业务服务层 (Service Layer)
  * @note    本层负责系统级功能的调度：配置管理、指令分发、事件通知。
  *          它通过依赖注入的方式，与具体的硬件平台解耦。
  ******************************************************************************
  */

#ifndef __LORA_SERVICE_H
#define __LORA_SERVICE_H

#include <stdint.h>
#include <stdbool.h>
#include "LoRaPlatConfig.h" // 全局配置结构体定义

// ============================================================
//                    1. 类型定义 (Types)
// ============================================================

/**
 * @brief 系统事件枚举
 */
typedef enum {
    LORA_EVT_INIT_DONE = 0,     ///< 初始化完成
    LORA_EVT_MSG_RECV,          ///< 收到业务数据
    LORA_EVT_MSG_SENT,          ///< 数据发送完成
    LORA_EVT_BIND_SUCCESS,      ///< ID 绑定成功
    LORA_EVT_CONFIG_SAVE,       ///< 配置已保存
    LORA_EVT_FACTORY_RESET      ///< 恢复出厂设置
} LoRa_Event_t;

/**
 * @brief 接收元数据
 */
typedef struct {
    int16_t rssi;   ///< 信号强度 (预留)
    int8_t  snr;    ///< 信噪比 (预留)
} LoRa_RxMeta_t;

/**
 * @brief 应用层适配接口 (依赖注入)
 * @note  App 层必须实现这些函数，以赋予 Service 层操作硬件的能力。
 */
typedef struct {
    // --- 必选接口 ---
    void (*SaveConfig)(const LoRa_Config_t *cfg);   ///< 保存配置到 Flash/NVS
    void (*LoadConfig)(LoRa_Config_t *cfg);         ///< 从 Flash/NVS 读取配置
    uint32_t (*GetTick)(void);                      ///< 获取系统毫秒数
    void (*SystemReset)(void);                      ///< 执行系统复位
    
    // --- 可选接口 ---
    uint32_t (*GetRandomSeed)(void);                ///< 获取随机数种子 (用于生成 UUID)
    
    // --- 业务回调 ---
    /**
     * @brief 收到业务数据回调
     * @param src_id 源设备 ID
     * @param data   数据指针
     * @param len    数据长度
     * @param meta   元数据 (RSSI/SNR)
     */
    void (*OnRecvData)(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta);
    
    /**
     * @brief 系统事件通知
     * @param evt 事件类型
     * @param arg 事件参数 (可选)
     */
    void (*OnEvent)(LoRa_Event_t evt, void *arg);

} LoRa_App_Adapter_t;

// ============================================================
//                    2. 核心接口 (API)
// ============================================================

/**
 * @brief  初始化服务层
 * @param  adapter: 应用层提供的适配器接口
 * @param  override_net_id: 强制覆盖的 NetID (0表示使用Flash配置)
 */
void Service_Init(const LoRa_App_Adapter_t *adapter, uint16_t override_net_id);

/**
 * @brief  服务层心跳 (必须在主循环调用)
 */
void Service_Run(void);

/**
 * @brief  发送业务数据
 * @param  data:      数据内容
 * @param  len:       数据长度
 * @param  target_id: 目标 ID (0xFFFF=广播)
 * @return true=请求已受理, false=系统忙
 */
bool Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id);

/**
 * @brief  检查系统是否空闲
 * @return true=空闲 (可休眠)
 */
bool Service_IsIdle(void);

/**
 * @brief  执行恢复出厂设置
 */
void Service_FactoryReset(void);



/**
 * @brief  获取当前系统配置 (只读)
 * @return 指向内部配置结构体的指针
 */
const LoRa_Config_t* Service_GetConfig(void);


#endif // __LORA_SERVICE_H
