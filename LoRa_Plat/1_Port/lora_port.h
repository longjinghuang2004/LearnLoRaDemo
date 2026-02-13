#ifndef __LORA_PORT_H
#define __LORA_PORT_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 硬件配置宏
// ============================================================
// 波特率定义 (如果不同平台波特率不同，可移至具体实现文件或Config)
//#define LORA_UART_BAUDRATE  115200

// ============================================================
//                    2. 核心接口 (API)
// ============================================================

/**
 * @brief  端口初始化 (GPIO, UART, DMA, NVIC, EXTI)
 * @note   具体实现由 lora_port_xxx.c 决定
 */
void Port_Init(void);

/**
 * @brief  设置 MD0 引脚电平
 * @param  level: true=High(配置模式), false=Low(通信模式)
 */
void Port_SetMD0(bool level);

/**
 * @brief  获取 AUX 引脚的实时电平 (直接读寄存器)
 * @return true=High(Busy), false=Low(Idle)
 */
bool Port_GetAUX_Raw(void);

/**
 * @brief  获取 AUX 的逻辑忙闲状态 (由中断维护)
 * @return true=Busy, false=Idle
 */
bool Port_IsAuxBusy(void);

/**
 * @brief  强制同步 AUX 状态 (消除初始化残留)
 */
void Port_SyncAuxState(void);

/**
 * @brief  控制 RST 引脚 (如果有)
 */
void Port_SetRST(bool level);

/**
 * @brief  启动 DMA 发送
 * @return 实际写入 DMA 的长度
 */
uint16_t Port_WriteData(const uint8_t *data, uint16_t len);

/**
 * @brief  从 DMA 循环缓冲区读取数据
 * @return 实际读取长度
 */
uint16_t Port_ReadData(uint8_t *buf, uint16_t max_len);

/**
 * @brief  清空接收缓冲区 (丢弃旧数据)
 */
void Port_ClearRxBuffer(void);

/**
 * @brief  获取系统毫秒数
 */
uint32_t Port_GetTick(void);

/**
 * @brief  获取随机数种子 (ADC悬空或其他熵源)
 */
uint32_t Port_GetRandomSeed(void);

/**
 * @brief  临时修改 UART 波特率 (用于救砖)
 */
void Port_ReInitUart(uint32_t baudrate);

#endif // __LORA_PORT_H
