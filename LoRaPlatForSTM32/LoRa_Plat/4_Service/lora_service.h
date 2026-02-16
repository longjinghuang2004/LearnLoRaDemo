#ifndef __LORA_SERVICE_H
#define __LORA_SERVICE_H

#include "lora_osal.h"
#include "LoRaPlatConfig.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 数据结构定义
// ============================================================


// [新增] 常用发送选项宏
#define LORA_OPT_CONFIRMED      (LoRa_SendOpt_t){ .NeedAck = true }
#define LORA_OPT_UNCONFIRMED    (LoRa_SendOpt_t){ .NeedAck = false }

typedef struct {
    int16_t rssi; 
    int8_t  snr;  
} LoRa_RxMeta_t;

typedef enum {
    LORA_EVENT_INIT_SUCCESS = 0,
    LORA_EVENT_BIND_SUCCESS,    
    LORA_EVENT_GROUP_UPDATE,    
    LORA_EVENT_CONFIG_START,    
    LORA_EVENT_CONFIG_COMMIT,   
    LORA_EVENT_FACTORY_RESET,   
    LORA_EVENT_REBOOT_REQ,      
    LORA_EVENT_MSG_RECEIVED,    
    
    // --- 发送相关事件 ---
    LORA_EVENT_MSG_SENT,        // 物理层发送完成 (DMA 传输结束)
    LORA_EVENT_TX_FINISHED,     // 发送流程彻底结束 (收到 ACK 或 广播发完)
    LORA_EVENT_TX_FAILED        // 发送失败 (重传次数耗尽)
    
} LoRa_Event_t;

// ============================================================
//                    2. 回调接口
// ============================================================
typedef struct {
    void (*SaveConfig)(const LoRa_Config_t *cfg);
    void (*LoadConfig)(LoRa_Config_t *cfg);
    uint32_t (*GetRandomSeed)(void); 
    void (*SystemReset)(void);       
    void (*OnRecvData)(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta);
    void (*OnEvent)(LoRa_Event_t event, void *arg);
} LoRa_Callback_t;

// ============================================================
//                    3. 全局函数
// ============================================================

void LoRa_Service_Init(const LoRa_Callback_t *callbacks, uint16_t override_net_id);
void LoRa_Service_Run(void);
// [修改] 必须增加 LoRa_SendOpt_t opt 参数
bool LoRa_Service_Send(const uint8_t *data, uint16_t len, uint16_t target_id, LoRa_SendOpt_t opt);

// [新增] 获取系统建议休眠时长 (Tickless 接口)
uint32_t LoRa_Service_GetSleepDuration(void);

void LoRa_Service_FactoryReset(void);

// 配置访问
const LoRa_Config_t* LoRa_Service_GetConfig(void);
void LoRa_Service_SetConfig(const LoRa_Config_t *cfg);

// 内部通知接口 (供 Command 模块调用)
void LoRa_Service_NotifyEvent(LoRa_Event_t event, void *arg);

#endif // __LORA_SERVICE_H
