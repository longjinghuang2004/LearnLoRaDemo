#include "Flash.h"
#include <string.h> // 用于 memcpy

/**
  * @brief  读取指定地址的一个字节 (8-bit)
  * @param  Address 要读取的地址
  * @retval 读取到的字节数据
  */
uint8_t Flash_ReadByte(uint32_t Address)
{
    // Flash是内存映射的，可以直接通过指针读取
    // 使用 volatile 防止编译器优化
    return *(volatile uint8_t*)Address;
}

/**
  * @brief  读取指定地址的一个字 (32-bit)
  * @param  Address 要读取的地址
  * @retval 读取到的字数据
  */
uint32_t Flash_ReadWord(uint32_t Address)
{
    return *(volatile uint32_t*)Address;
}

/**
  * @brief  擦除指定的Flash页面
  * @param  PageAddress 要擦除页面的任一地址
  * @retval 无
  */
void Flash_ErasePage(uint32_t PageAddress)
{
    FLASH_Unlock();                     // 解锁Flash
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR); // 清除所有标志位
    FLASH_ErasePage(PageAddress);       // 擦除页面
    FLASH_Lock();                       // 锁定Flash
}

/**
  * @brief  在指定地址写入一个字 (32-bit)
  * @param  Address 写入地址，必须是4的倍数
  * @param  Data 要写入的32位数据
  * @retval 无
  */
void Flash_ProgramWord(uint32_t Address, uint32_t Data)
{
    FLASH_Unlock();
    FLASH_ClearFlag(FLASH_FLAG_BSY | FLASH_FLAG_EOP | FLASH_FLAG_PGERR | FLASH_FLAG_WRPRTERR);
    FLASH_ProgramWord(Address, Data);
    FLASH_Lock();
}

/**
  * @brief  从Flash读取整个模型参数结构体
  * @param  params 指向要填充的RAM结构体变量
  * @note   由于Flash是内存映射的，直接使用memcpy是最高效、最安全的方式。
  */
void Flash_ReadModelParams(RiskModelParameters* params)
{
    memcpy(params, (void*)FLASH_STORE_ADDR, sizeof(RiskModelParameters));
}

/**
  * @brief  将整个模型参数结构体写入Flash
  * @param  params 指向包含待写入数据的RAM结构体变量
  * @note   此函数使用32位字写入，并能正确处理结构体大小不是4字节整数倍的情况。
  */
void Flash_WriteModelParams(const RiskModelParameters* params)
{
    // 1. 擦除页面
    Flash_ErasePage(FLASH_STORE_ADDR);
    
    // 2. 计算需要写入的32位字的数量和剩余字节数
    uint32_t size_in_words = sizeof(RiskModelParameters) / 4;
    uint8_t remainder_bytes = sizeof(RiskModelParameters) % 4;
    
    // 3. 将结构体指针转换为 uint32_t 指针，用于按字访问
    uint32_t* pData = (uint32_t*)params;
    
    // 4. 循环写入所有完整的字
    for (uint32_t i = 0; i < size_in_words; i++)
    {
        Flash_ProgramWord(FLASH_STORE_ADDR + i * 4, pData[i]);
    }
    
    // 5. 如果有剩余的字节 (1, 2, 或 3)，则特殊处理
    if (remainder_bytes > 0)
    {
        // 计算剩余字节的起始地址
        uint32_t remainder_addr = FLASH_STORE_ADDR + size_in_words * 4;
        uint8_t* remainder_data_ptr = (uint8_t*)params + size_in_words * 4;
        
        // 创建一个临时的32位变量，并用Flash的擦除值(0xFF)填充
        uint32_t last_word = 0xFFFFFFFF;
        
        // 使用memcpy将剩余的字节复制到临时变量的低位
        memcpy(&last_word, remainder_data_ptr, remainder_bytes);
        
        // 将这个包含剩余字节的字写入Flash
        Flash_ProgramWord(remainder_addr, last_word);
    }
}
