#include "lora_driver.h"
#include "lora_port.h"

// Init 在 core 中实现，这里只实现透传接口

bool LoRa_Driver_AsyncSend(const uint8_t *data, uint16_t len) {
    // 1. 检查 AUX
    if (LoRa_Port_GetAUX()) return false;
    
    // 2. 启动 DMA
    return (LoRa_Port_TransmitData(data, len) > 0);
}

uint16_t LoRa_Driver_Read(uint8_t *buf, uint16_t max_len) {
    return LoRa_Port_ReceiveData(buf, max_len);
}

bool LoRa_Driver_IsBusy(void) {
    return LoRa_Port_GetAUX() || LoRa_Port_IsTxBusy();
}
