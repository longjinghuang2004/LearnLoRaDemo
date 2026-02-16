/**
  ******************************************************************************
  * @file    lora_port.h
  * @author  LoRaPlat Team
  * @brief   硬件接口层 (Port Layer) 定义 V3.3.0
  *          负责屏蔽具体 MCU 的 GPIO/UART/DMA 差异。
  ******************************************************************************
  */

#ifndef __LORA_PORT_H
#define __LORA_PORT_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 初始化与配置
// ============================================================

/**
 * @brief  端口初始化 (GPIO, UART, DMA, NVIC)
 * @param  baudrate: 初始波特率
 */
void LoRa_Port_Init(uint32_t baudrate);

/**
 * @brief  重新配置 UART 波特率 (用于救砖或模式切换)
 */
void LoRa_Port_ReInitUart(uint32_t baudrate);

// ============================================================
//                    2. 引脚控制 (GPIO)
// ============================================================

/**
 * @brief  设置 MD0 引脚 (模式控制)
 * @param  level: true=High(配置模式), false=Low(通信模式)
 */
void LoRa_Port_SetMD0(bool level);

/**
 * @brief  设置 RST 引脚 (复位控制)
 * @param  level: true=High, false=Low(复位)
 */
void LoRa_Port_SetRST(bool level);

/**
 * @brief  获取 AUX 引脚电平 (忙闲指示)
 * @return true=High(Busy), false=Low(Idle)
 */
bool LoRa_Port_GetAUX(void);

// ============================================================
//                    3. 发送接口 (TX)
// ============================================================

/**
 * @brief  查询发送硬件是否忙碌
 * @return true=忙 (DMA正在搬运), false=闲
 */
bool LoRa_Port_IsTxBusy(void);

/**
 * @brief  发送数据 (启动 DMA)
 * @param  data: 数据指针
 * @param  len:  数据长度
 * @return 实际发送长度 (0表示硬件忙，发送失败)
 * @note   内部具有原子性保护，防止覆写缓冲区
 */
uint16_t LoRa_Port_TransmitData(const uint8_t *data, uint16_t len);

// ============================================================
//                    4. 接收接口 (RX)
// ============================================================

/**
 * @brief  从 DMA 循环缓冲区读取数据
 * @param  buf: 目标缓冲区
 * @param  max_len: 最大读取长度
 * @return 实际读取到的字节数
 */
uint16_t LoRa_Port_ReceiveData(uint8_t *buf, uint16_t max_len);

/**
 * @brief  清空接收缓冲区 (丢弃旧数据)
 * @note   通常在发送 AT 指令前调用，确保收到的是最新的响应
 */
void LoRa_Port_ClearRxBuffer(void);

// ============================================================
//                    5. 其他能力
// ============================================================

/**
 * @brief  获取物理熵源 (用于生成随机种子)
 * @return 32位随机数
 */
uint32_t LoRa_Port_GetEntropy32(void);

/**
 * @brief  同步 AUX 状态 (消除中断抖动或初始化残留)
 */
void LoRa_Port_SyncAuxState(void);

#endif // __LORA_PORT_H
