#ifndef __FLASH_H
#define __FLASH_H

#include "stm32f10x.h"
#include "Model.h" 
#include "LoRaPlatConfig.h" // 引入 LoRa 配置定义

// Flash 布局 (STM32F103C8T6 64KB)
// Page 63 (Last): 算法模型参数
#define FLASH_MODEL_ADDR    0x0800FC00
// [兼容性修复] 旧代码使用 FLASH_STORE_ADDR，将其指向 FLASH_MODEL_ADDR
#define FLASH_STORE_ADDR    FLASH_MODEL_ADDR

// Page 62 (2nd Last): LoRa 系统配置
#define FLASH_LORA_ADDR     0x0800F800

/* --- 基础函数 --- */
void Flash_ErasePage(uint32_t PageAddress);
void Flash_ProgramWord(uint32_t Address, uint32_t Data);

// [兼容性修复] 恢复基础读取函数声明
uint8_t Flash_ReadByte(uint32_t Address);
uint32_t Flash_ReadWord(uint32_t Address);

/* --- 算法模型存取 --- */
void Flash_ReadModelParams(RiskModelParameters* params);
void Flash_WriteModelParams(const RiskModelParameters* params);

/* --- LoRa 配置存取 --- */
void Flash_ReadLoRaConfig(LoRa_Config_t* cfg);
void Flash_WriteLoRaConfig(const LoRa_Config_t* cfg);

#endif
