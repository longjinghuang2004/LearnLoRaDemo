#include "mod_lora.h"
#include "lora_driver.h"

void LoRa_Core_Init(void) {
    Drv_Init();
    // 执行智能配置 (阻塞式)
    if (!Drv_SmartConfig()) {
        // 初始化失败处理，例如死循环闪灯
        while(1); 
    }
}

void LoRa_Core_SetMode(uint8_t mode) {
    Drv_SetMode(mode);
}

bool LoRa_Core_IsBusy(void) {
    return Drv_IsBusy();
}

uint16_t LoRa_Core_SendRaw(const uint8_t *data, uint16_t len) {
    return Drv_Write(data, len);
}

uint16_t LoRa_Core_ReadRaw(uint8_t *buf, uint16_t max_len) {
    return Drv_Read(buf, max_len);
}
