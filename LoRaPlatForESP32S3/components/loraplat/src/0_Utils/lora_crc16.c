/**
  ******************************************************************************
  * @file    lora_crc16.c
  * @author  LoRaPlat Team
  * @brief   CRC16-CCITT (XMODEM) 实现
  ******************************************************************************
  */

#include "lora_crc16.h"

uint16_t LoRa_CRC16_Calculate(const uint8_t *data, uint16_t length) {
    uint16_t crc = 0x0000;
    
    while (length--) {
        crc ^= (uint16_t)(*data++) << 8;
        for (int i = 0; i < 8; i++) {
            if (crc & 0x8000) {
                crc = (crc << 1) ^ 0x1021;
            } else {
                crc <<= 1;
            }
        }
    }
    return crc;
}

uint8_t LoRa_CRC16_Verify(const uint8_t *data, uint16_t length, uint16_t expected_crc) {
    uint16_t calc = LoRa_CRC16_Calculate(data, length);
    return (calc == expected_crc) ? 1 : 0;
}
