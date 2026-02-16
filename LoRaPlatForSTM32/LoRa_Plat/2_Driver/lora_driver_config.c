#include "lora_driver.h"
#include <stdio.h>

// --- ATK-LORA-01 指令集 ---

const char* Drv_GetAtCmd_Reset(void) {
    return "AT+RESET\r\n";
}

const char* Drv_GetAtCmd_Mode(uint8_t mode) {
    // 0=透传, 1=定向
    return (mode == 0) ? "AT+TMODE=0\r\n" : "AT+TMODE=1\r\n";
}

void Drv_GetAtCmd_Rate(uint8_t channel, uint8_t rate, char *buf) {
    // 映射速率: 0->0.3k ... 5->19.2k
    // ATK 默认 5 (19.2k)
    if (rate > 5) rate = 5;
    sprintf(buf, "AT+WLRATE=%d,%d\r\n", channel, rate);
}

void Drv_GetAtCmd_Addr(uint16_t addr, char *buf) {
    sprintf(buf, "AT+ADDR=%02X,%02X\r\n", (addr >> 8) & 0xFF, addr & 0xFF);
}

void Drv_GetAtCmd_Power(uint8_t power, char *buf) {
    // 0=11dBm, 1=14dBm, 2=17dBm, 3=20dBm
    if (power > 3) power = 3;
    sprintf(buf, "AT+TPOWER=%d\r\n", power);
}
