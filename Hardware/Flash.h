#ifndef __FLASH_H
#define __FLASH_H

#include "stm32f10x.h"
#include "Model.h" // 引入Model.h以识别RiskModelParameters类型

// 定义参数存储区的起始地址 (Flash最后一页)
#define FLASH_STORE_ADDR    0x0800FC00

/* --- 底层基础函数 --- */
uint8_t Flash_ReadByte(uint32_t Address);
uint32_t Flash_ReadWord(uint32_t Address);
void Flash_ErasePage(uint32_t PageAddress);
void Flash_ProgramWord(uint32_t Address, uint32_t Data);

/* --- 高层应用函数 --- */
void Flash_ReadModelParams(RiskModelParameters* params);
void Flash_WriteModelParams(const RiskModelParameters* params);

#endif
