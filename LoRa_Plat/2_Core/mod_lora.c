#include "mod_lora.h"
#include "lora_port.h"
#include <string.h>

void LoRa_Core_Init(void)
{
    Port_Init();
    // 默认进入通信模式
    LoRa_Core_SetMode(0);
}

void LoRa_Core_SetMode(uint8_t mode)
{
    // mode 0 -> MD0 Low (Comm)
    // mode 1 -> MD0 High (Config)
    Port_SetMD0(mode == 1);
}

bool LoRa_Core_IsBusy(void)
{
    return Port_GetAUX();
}

void LoRa_Core_HardReset(void)
{
    // 简单的复位时序，这里可以使用阻塞延时，因为复位通常发生在系统异常时
    // 但为了严格遵守非阻塞原则，我们只提供动作接口，或者在这里破例使用微秒延时
    Port_SetRST(false);
    // 实际应用中，复位脉冲需要持续一段时间。
    // 如果 Port 层没有实现 Delay，这里可能需要依赖外部调用逻辑。
    // 暂且留空，依赖 Port 实现。
    Port_SetRST(true);
}

uint16_t LoRa_Core_SendRaw(const uint8_t *data, uint16_t len)
{
    return Port_WriteData(data, len);
}

uint16_t LoRa_Core_ReadRaw(uint8_t *buf, uint16_t max_len)
{
    return Port_ReadData(buf, max_len);
}

bool LoRa_Core_CheckResponse(const uint8_t *buf, uint16_t len, const char *expect_str)
{
    if (len == 0 || expect_str == NULL) return false;
    
    // 简单的子串查找
    // 注意：buf 可能不是以 \0 结尾的，所以不能直接用 strstr
    // 这里手动实现一个简单的内存搜索
    uint16_t str_len = strlen(expect_str);
    if (len < str_len) return false;
    
    for (uint16_t i = 0; i <= len - str_len; i++)
    {
        if (memcmp(&buf[i], expect_str, str_len) == 0)
        {
            return true;
        }
    }
    return false;
}
