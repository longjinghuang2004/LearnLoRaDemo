/**
  ******************************************************************************
  * @file    lora_service.h
  * @author  LoRaPlat Team
  * @brief   LoRa 业务服务层接口 (V3.9.2)
  *          负责协调 Manager, Config, Monitor 以及对外提供统一的业务接口。
  *          支持 OTA 远程配置、动态 ACK 策略及系统软重启。
  ******************************************************************************
  */

#ifndef __LORA_SERVICE_H
#define __LORA_SERVICE_H

#include "lora_osal.h"
#include "LoRaPlatConfig.h"

#include "lora_manager.h"

#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 数据结构与宏定义
// ============================================================

/**
 * @brief 发送选项宏定义
 * @note  LoRa_SendOpt_t 结构体定义在 LoRaPlatConfig.h 中
 */
#define LORA_OPT_CONFIRMED      (LoRa_SendOpt_t){ .NeedAck = true }  /*!< 需要 ACK 确认 (可靠传输) */
#define LORA_OPT_UNCONFIRMED    (LoRa_SendOpt_t){ .NeedAck = false } /*!< 不需要 ACK (发后即忘) */

/**
 * @brief 接收数据元信息
 */
typedef struct {
    int16_t rssi; /*!< 接收信号强度指示 (dBm) */
    int8_t  snr;  /*!< 信噪比 (dB) */
} LoRa_RxMeta_t;

/**
 * @brief LoRa 系统事件枚举
 */
typedef enum {
    LORA_EVENT_INIT_SUCCESS = 0, /*!< 初始化成功 (包括软重启完成) */
    LORA_EVENT_BIND_SUCCESS,     /*!< 绑定成功 (暂未启用) */
    LORA_EVENT_GROUP_UPDATE,     /*!< 组 ID 更新 */
    
    LORA_EVENT_CONFIG_START,     /*!< 开始配置流程 */
    LORA_EVENT_CONFIG_COMMIT,    /*!< 配置已提交 (应用层需在此事件中将 arg 转为 Config 指针并写入 Flash) */
    LORA_EVENT_FACTORY_RESET,    /*!< 恢复出厂设置 */
    
    LORA_EVENT_REBOOT_REQ,       /*!< 请求重启 (系统进入倒计时，应用层无需处理，仅作通知) */
    
    LORA_EVENT_MSG_RECEIVED,     /*!< 收到业务数据 */
    
    // --- 发送相关事件 ---
    LORA_EVENT_MSG_SENT,         /*!< 物理层发送完成 (DMA 传输结束) */
	
    // [修改] 带 ID 的发送结果事件
    // arg 参数将指向 LoRa_MsgID_t 类型的变量
    LORA_EVENT_TX_SUCCESS_ID,    /*!< 发送成功 (收到 ACK 或 UNCONFIRMED 发送完成) */
    LORA_EVENT_TX_FAILED_ID      /*!< 发送失败 (重传次数耗尽) */
    
} LoRa_Event_t;

// ============================================================
//                    2. 回调接口定义
// ============================================================

/**
 * @brief 应用层回调函数集
 */
typedef struct {
    /**
     * @brief 保存配置到非易失性存储器 (NVS/Flash)
     * @param cfg 待保存的配置结构体指针
     */
    void (*SaveConfig)(const LoRa_Config_t *cfg);

    /**
     * @brief 从非易失性存储器加载配置
     * @param cfg 用于接收配置的结构体指针
     */
    void (*LoadConfig)(LoRa_Config_t *cfg);

    /**
     * @brief 获取随机种子 (用于生成 UUID 或 随机避退)
     * @return 32位随机数
     */
    uint32_t (*GetRandomSeed)(void); 

    /**
     * @brief 系统硬件复位接口 (看门狗兜底用)
     */
    void (*SystemReset)(void);       

    /**
     * @brief 接收数据回调
     * @param src_id 源设备逻辑 ID
     * @param data   数据指针
     * @param len    数据长度
     * @param meta   元数据 (RSSI/SNR)
     */
    void (*OnRecvData)(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta);

    /**
     * @brief 系统事件回调
     * @param event 事件类型
     * @param arg   事件参数 (根据事件类型转换)
     */
    void (*OnEvent)(LoRa_Event_t event, void *arg);

} LoRa_Callback_t;

// ============================================================
//                    3. 核心 API
// ============================================================

/**
 * @brief  初始化 LoRa 服务层
 * @param  callbacks 应用层回调函数指针
 * @param  override_net_id 覆盖默认 NetID (0 表示使用 Config 中的值)
 */
void LoRa_Service_Init(const LoRa_Callback_t *callbacks, uint16_t override_net_id);

/**
 * @brief  服务层主循环 (需在 main loop 中周期调用)
 * @note   处理协议栈逻辑、监视器及软重启任务
 */
void LoRa_Service_Run(void);
/**
 * @brief  发送数据
 * @param  data      数据指针
 * @param  len       数据长度
 * @param  target_id 目标逻辑 ID (0xFFFF 为广播)
 * @param  opt       发送选项 (LORA_OPT_CONFIRMED / LORA_OPT_UNCONFIRMED)
 * @return >0: 成功入队的消息 ID (1~65535)
 *         0:  队列满或忙，发送失败
 */
LoRa_MsgID_t LoRa_Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id, LoRa_SendOpt_t opt);

/**
 * @brief  请求协议栈软重启 (异步安全)
 * @note   调用此函数后，Service 层会在下一次 Run 循环的安全点自动重新初始化驱动和管理器。
 *         常用于 OTA 配置生效或错误恢复。
 */
void LoRa_Service_SoftReset(void);

/**
 * @brief  恢复出厂设置
 * @note   清除配置并触发软重启
 */
void LoRa_Service_FactoryReset(void);

/**
 * @brief  获取当前系统建议的休眠时长 (Tickless 模式支持)
 * @return 建议休眠毫秒数 (0 表示忙，不可休眠)
 */
uint32_t LoRa_Service_GetSleepDuration(void);

/**
 * @brief  查询是否忙碌 (包含发送队列、重传等待等)
 * @return true=忙, false=空闲 (可休眠)
 */
bool LoRa_Service_IsBusy(void);

/**
 * @brief  注册安全算法 (透传给 Manager 层)
 * @param  cipher: 算法接口指针 (NULL 表示注销)
 */
void LoRa_Service_RegisterCipher(const LoRa_Cipher_t *cipher);


/**
 * @brief  [主循环调用] 检查系统是否可以进入休眠
 * @return true = 可以休眠 (业务空闲且无硬件中断挂起)
 *         false = 不可休眠 (忙碌或有新事件)
 * @note   该函数会聚合 Manager、Driver 和 Port 的状态。
 *         如果返回 true，主循环可以安全调用 __WFI()。
 */
bool LoRa_Service_CanSleep(void);


// --- 配置访问 ---
const LoRa_Config_t* LoRa_Service_GetConfig(void);
void LoRa_Service_SetConfig(const LoRa_Config_t *cfg);

// --- 内部接口 (供 Command 模块使用) ---
void LoRa_Service_NotifyEvent(LoRa_Event_t event, void *arg);

#endif // __LORA_SERVICE_H
