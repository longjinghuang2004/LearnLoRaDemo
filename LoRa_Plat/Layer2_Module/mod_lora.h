#ifndef __MOD_LORA_H
#define __MOD_LORA_H

#include "stm32f10x.h"
#include <stdbool.h> // 引入 bool 类型

// --- LoRa 参数定义 ---
#define LORA_ADDR_A     0x0001
#define LORA_ADDR_B     0x0002
#define LORA_CHN        23

// --- 函数接口 ---
void LoRa_Init(void);
void LoRa_SetMode_Config(void); 
void LoRa_SetMode_Trans(void);  

// [修改] 返回 bool 类型，指示初始化是否成功
bool LoRa_Init_And_SelfCheck(uint16_t addr);

#endif
