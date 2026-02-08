#ifndef __MOD_LORA_H
#define __MOD_LORA_H

#include "stm32f10x.h"

// --- LoRa 参数定义 ---
// 目标配置: 地址0/1, 信道23(433MHz), 空中速率19.2k, 串口115200, 8N1
#define LORA_ADDR_A     0x0001
#define LORA_ADDR_B     0x0002
#define LORA_CHN        23

// --- 函数接口 ---
void LoRa_Init(void);
void LoRa_SetMode_Config(void); // 进入配置模式
void LoRa_SetMode_Trans(void);  // 进入透传模式

// 自动配置函数 (addr: 目标地址)
void LoRa_AutoConfig(uint16_t addr);

#endif
