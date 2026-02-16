/**
  ******************************************************************************
  * @file    lora_crc16.h
  * @author  LoRaPlat Team
  * @brief   CRC16-CCITT (XMODEM) 计算工具
  *          Poly: 0x1021, Init: 0x0000
  ******************************************************************************
  */

#ifndef __LORA_CRC16_H
#define __LORA_CRC16_H

#include <stdint.h>
#include <stddef.h>

/**
 * @brief  计算 CRC16
 * @param  data: 数据指针
 * @param  length: 数据长度
 * @return CRC16 校验码
 */
uint16_t LoRa_CRC16_Calculate(const uint8_t *data, uint16_t length);

/**
 * @brief  校验 CRC16
 * @param  data: 数据指针
 * @param  length: 数据长度
 * @param  expected_crc: 期望的校验码
 * @return 1=校验通过, 0=校验失败
 */
uint8_t LoRa_CRC16_Verify(const uint8_t *data, uint16_t length, uint16_t expected_crc);

#endif // __LORA_CRC16_H
