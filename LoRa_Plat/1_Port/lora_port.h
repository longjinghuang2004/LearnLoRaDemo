#ifndef __LORA_PORT_H
#define __LORA_PORT_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 硬件钩子定义 (V-Table)
// ============================================================

/**
 * @brief 物理层操作钩子结构体
 * @note  Driver 层将通过这些函数指针操作硬件，实现硬件解耦
 */
typedef struct {
    // --- 基础能力 ---
    void     (*Init)(void);             // 硬件初始化
    uint32_t (*GetTick)(void);          // 获取系统毫秒数
    
    // --- 物理发送 (TX) ---
    /**
     * @brief 启动物理发送
     * @param data 数据指针
     * @param len  数据长度
     * @note  对于 UART，启动 DMA；对于 SPI，写 FIFO 并置位 TX
     */
    void     (*Phy_StartTx)(const uint8_t *data, uint16_t len);
    
    // --- 物理状态 (State) ---
    /**
     * @brief 查询物理层是否忙碌
     * @return true=忙 (不可发送), false=闲 (可发送)
     * @note   必须同时检查 模块状态(AUX/DIO) 和 MCU发送状态(DMA)
     */
    bool     (*Phy_IsBusy)(void);
    
    /**
     * @brief 获取物理层推荐的冷却时间
     * @return 毫秒数 (UART通常50ms, SPI通常2ms)
     */
    uint32_t (*Phy_GetRecoveryTime)(void);
    
    // --- 物理控制 (Control) ---
    void     (*Phy_SetMode)(bool config_mode); // true=配置(MD0=1), false=透传(MD0=0)
    void     (*Phy_HardReset)(void);           // 拉动 RST 引脚
    
    // --- 物理接收 (RX) ---
    /**
     * @brief 从底层缓冲区读取数据
     * @param buf     目标缓冲区
     * @param max_len 最大读取长度
     * @return 实际读取的字节数
     */
    uint16_t (*Phy_Read)(uint8_t *buf, uint16_t max_len);
    
    /**
     * @brief 清空底层接收缓冲区
     * @note  在发送指令前调用，防止读取到旧数据
     */
    void     (*Phy_ClearRx)(void);

} LoRa_Port_Hooks_t;

// ============================================================
//                    2. 全局实例声明
// ============================================================

// 全局钩子实例 (Driver 层将使用此对象)
extern const LoRa_Port_Hooks_t g_LoRaPort;

// 显式初始化函数 (在 main 中调用，绑定 STM32 硬件)
void Port_Init_STM32(void);

#endif // __LORA_PORT_H
