#ifndef __LORA_PORT_H
#define __LORA_PORT_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 硬件钩子定义
// ============================================================
typedef struct {
    void     (*Init)(void);             
    uint32_t (*GetTick)(void);          
    
    void     (*Phy_StartTx)(const uint8_t *data, uint16_t len);
    bool     (*Phy_IsBusy)(void);
    uint32_t (*Phy_GetRecoveryTime)(void);
    
    void     (*Phy_SetMode)(bool config_mode); 
    void     (*Phy_HardReset)(void);           
    
    uint16_t (*Phy_Read)(uint8_t *buf, uint16_t max_len);
    void     (*Phy_ClearRx)(void);

} LoRa_Port_Hooks_t;

// ============================================================
//                    2. 全局实例声明
// ============================================================

// [关键] 暴露这个变量，Driver 层才能调用
extern const LoRa_Port_Hooks_t g_LoRaPort;

// 显式初始化函数
void Port_Init_STM32(void);

// 辅助函数声明 (供 Service 层调用)
void Port_ClearRxBuffer(void); 
uint32_t Port_GetTick(void);

#endif
