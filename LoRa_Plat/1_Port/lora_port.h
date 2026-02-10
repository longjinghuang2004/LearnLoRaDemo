#ifndef __LORA_PORT_H
#define __LORA_PORT_H

#include <stdint.h>

/* ========================================================================== */
/*                                 硬件初始化                                  */
/* ========================================================================== */

/**
 * @brief 初始化所有 LoRa 相关的硬件 (GPIO, UART, DMA, NVIC)
 */
void Port_Hardware_Init(void);

/* ========================================================================== */
/*                                 通道 A 接口                                 */
/* ========================================================================== */

// UART 收发
uint16_t Port_UART_A_Write(const uint8_t *data, uint16_t len);
uint16_t Port_UART_A_Read(uint8_t *buf, uint16_t max_len);

// GPIO 控制
void Port_Set_MD0_A(uint8_t level);
void Port_Set_Reset_A(uint8_t level);
uint8_t Port_Read_AUX_A(void);

/* ========================================================================== */
/*                                 通道 B 接口 (预留)                          */
/* ========================================================================== */

// UART 收发
uint16_t Port_UART_B_Write(const uint8_t *data, uint16_t len);
uint16_t Port_UART_B_Read(uint8_t *buf, uint16_t max_len);

// GPIO 控制
void Port_Set_MD0_B(uint8_t level);
void Port_Set_Reset_B(uint8_t level);
uint8_t Port_Read_AUX_B(void);

/* ========================================================================== */
/*                                 系统服务                                    */
/* ========================================================================== */

void Port_DelayMs(uint32_t ms);
uint32_t Port_GetTick(void);

#endif // __LORA_PORT_H
