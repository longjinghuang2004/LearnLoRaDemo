#include "lora_driver.h"
#include "lora_port.h"
//#include "Delay.h"
//#include "Serial.h"

#include "lora_osal.h"

#include <string.h>
#include <stdio.h>

// --- 内部辅助: 阻塞式 AT 发送 ---
static bool _SendAT(const char *cmd, const char *expect, uint32_t timeout) {
    Port_ClearRxBuffer();
    Port_WriteData((uint8_t*)cmd, strlen(cmd));
    
    uint32_t start = OSAL_GetTick();
    char rx_buf[128];
    uint16_t rx_idx = 0;
    
    while (OSAL_GetTick() - start < timeout) {
        uint8_t byte;
        if (Port_ReadData(&byte, 1) > 0) {
            rx_buf[rx_idx++] = byte;
            if (rx_idx >= 127) rx_idx = 127;
            rx_buf[rx_idx] = '\0';
            if (strstr(rx_buf, expect)) {
                OSAL_DelayMs(20); // 稍作延时确保模块处理完毕
                Port_ClearRxBuffer();
                return true;
            }
        }
    }
    return false;
}

// --- 救砖逻辑 ---
static bool _Unbrick(void) {
    LORA_LOG("[DRV] Unbricking...\r\n");
    
    // 1. 尝试 9600
    Port_ReInitUart(9600);
    OSAL_DelayMs(100);
    if (_SendAT("AT\r\n", "OK", 200)) {
        // 2. 强制改回 115200
        if (_SendAT("AT+UART=7,0\r\n", "OK", 500)) {
            Port_ReInitUart(115200);
            OSAL_DelayMs(100);
            if (_SendAT("AT\r\n", "OK", 200)) return true;
        }
    }
    
    // 恢复默认波特率，避免影响后续
    Port_ReInitUart(115200);
    return false;
}

// --- 核心接口实现 ---

bool Drv_Init(const LoRa_Config_t *cfg) {
    Port_Init();
    
    // 1. 进入配置模式
    Port_SetMD0(true);
    OSAL_DelayMs(600); // 等待模式切换稳定
    
    bool link_ok = false;
    
    // 2. 握手尝试 (3次)
    for (int i = 0; i < 3; i++) {
        if (_SendAT("AT\r\n", "OK", 200)) {
            link_ok = true;
            break;
        }
        LORA_LOG("[DRV] Handshake Fail %d/3\r\n", i+1);
    }
    
    // 3. 救砖尝试
    if (!link_ok) {
        if (_Unbrick()) link_ok = true;
    }
    
    if (!link_ok) {
        LORA_LOG("[DRV] Fatal: Module Dead!\r\n");
        return false;
    }
    
    // 4. 应用配置
    char cmd[64];
    bool cfg_ok = true;
    
    Drv_GetAtCmd_Addr(cfg->hw_addr, cmd);
    if (!_SendAT(cmd, "OK", 500)) cfg_ok = false;
    
    Drv_GetAtCmd_Rate(cfg->channel, cfg->air_rate, cmd);
    if (!_SendAT(cmd, "OK", 500)) cfg_ok = false;
    
    Drv_GetAtCmd_Power(cfg->power, cmd);
    if (!_SendAT(cmd, "OK", 500)) cfg_ok = false;
    
    if (!_SendAT(Drv_GetAtCmd_Mode(cfg->tmode), "OK", 500)) cfg_ok = false;
    
    // 5. 退出配置模式
    Port_SetMD0(false);
    OSAL_DelayMs(600); 
    while(Port_GetAUX_Raw()); // 等待 AUX 变低
    
    // [关键修复] 强制同步 AUX 状态，消除初始化残留
    Port_SyncAuxState();
    
    Port_ClearRxBuffer();
    return cfg_ok;
}

//warning!!!

#if (LORA_DEBUG_PRINT)

#endif



bool Drv_AsyncSend(const uint8_t *data, uint16_t len) {
    // 1. 唯一的阻塞条件：物理引脚 AUX 显示忙
    // 只要 AUX 是闲的，无论 DMA 状态如何，我们都认为可以发送
    if (Port_IsAuxBusy()) {
        // 双重检查：防止中断漏触发导致的软件标志位虚假忙碌
        if (Port_GetAUX_Raw()) {
            return false; // 真的忙
        } else {
            // 软件说忙，硬件说闲 -> 自愈
            Port_SyncAuxState();
        }
    }
    
    // 2. 启动 DMA (Port 层会暴力重置 DMA，确保发送成功)
    Port_WriteData(data, len);
    return true;
}


uint16_t Drv_Read(uint8_t *buf, uint16_t max_len) {
    return Port_ReadData(buf, max_len);
}

bool Drv_IsBusy(void) {
    return Port_IsAuxBusy();
}
