/**
  ******************************************************************************
  * @file    lora_driver_core.c
  * @author  LoRaPlat Team
  * @brief   LoRa 驱动核心逻辑 (策略层)
  ******************************************************************************
  */

#include "lora_driver.h"
#include "lora_at_command_engine.h"
#include "lora_port.h"
#include "lora_osal.h"
#include <stdio.h>

// ATK-LORA-01 硬件规定：配置模式(MD0=1)下固定使用 115200
#define ATK_LORA_CONFIG_BAUDRATE    115200

// 辅助：波特率转参数
static int _GetBaudParam(uint32_t baudrate) {
    switch(baudrate) {
        case 1200:   return 0;
        case 2400:   return 1;
        case 4800:   return 2;
        case 9600:   return 3;
        case 19200:  return 4;
        case 38400:  return 5;
        case 57600:  return 6;
        case 115200: return 7;
        default:     return 3; 
    }
}

bool LoRa_Driver_Init(const LoRa_Config_t *cfg) {
    // 1. 端口底层初始化
    LoRa_Port_Init(ATK_LORA_CONFIG_BAUDRATE); 
    LoRa_AT_Init();
    
    LORA_LOG("[DRV] Init Start. Target Baud: %d\r\n", LORA_TARGET_BAUDRATE);

    // 2. 进入配置模式
    LoRa_Port_SetMD0(true);
    OSAL_DelayMs(600); 
    
    // 3. 强制 MCU 串口切换到 115200
    LoRa_Port_ReInitUart(ATK_LORA_CONFIG_BAUDRATE);
    OSAL_DelayMs(100); 
    
    // 4. 握手检查
    bool link_ok = false;
    for (int i = 0; i < 3; i++) {
        if (LoRa_AT_Execute("AT\r\n", "OK", 200) == AT_STATUS_OK) {
            link_ok = true;
            LORA_LOG("[DRV] Handshake OK\r\n");
            break;
        }
        OSAL_DelayMs(100);
    }
    
    if (!link_ok) {
        LORA_LOG("[DRV] Handshake Fail!\r\n");
        LoRa_Port_ReInitUart(LORA_TARGET_BAUDRATE);
        return false;
    }
    
    // 5. 应用配置参数
    char cmd[64];
    bool cfg_ok = true;
    
    // 5.1 设置地址
    sprintf(cmd, "AT+ADDR=%02X,%02X\r\n", (cfg->hw_addr >> 8) & 0xFF, cfg->hw_addr & 0xFF);
    if (LoRa_AT_Execute(cmd, "OK", 500) != AT_STATUS_OK) cfg_ok = false;
    
    // 5.2 设置空速和信道
    sprintf(cmd, "AT+WLRATE=%d,%d\r\n", cfg->channel, cfg->air_rate);
    if (LoRa_AT_Execute(cmd, "OK", 500) != AT_STATUS_OK) cfg_ok = false;
    
    // 5.3 设置功率
    sprintf(cmd, "AT+TPOWER=%d\r\n", cfg->power);
    if (LoRa_AT_Execute(cmd, "OK", 500) != AT_STATUS_OK) cfg_ok = false;
    
    // 5.4 设置传输模式
    const char *mode_cmd = (cfg->tmode == 0) ? "AT+TMODE=0\r\n" : "AT+TMODE=1\r\n";
    if (LoRa_AT_Execute(mode_cmd, "OK", 500) != AT_STATUS_OK) cfg_ok = false;

    // 5.5 设置波特率
    int baud_param = _GetBaudParam(LORA_TARGET_BAUDRATE);
    sprintf(cmd, "AT+UART=%d,0\r\n", baud_param);
    if (LoRa_AT_Execute(cmd, "OK", 500) != AT_STATUS_OK) cfg_ok = false;
    
    // 6. 退出配置模式
    LoRa_Port_SetMD0(false);
    LORA_LOG("[DRV] Exiting Config Mode...\r\n");
    
    // 7. 等待重启
    OSAL_DelayMs(100); 
    uint32_t wait_start = OSAL_GetTick();
    while(!LoRa_Port_GetAUX()) {
         if (OSAL_GetTick() - wait_start > 500) break; 
    }
    wait_start = OSAL_GetTick();
    while(LoRa_Port_GetAUX()) {
        if (OSAL_GetTick() - wait_start > 2000) break;
    }
    
    // 8. 切回目标波特率
    LoRa_Port_ReInitUart(LORA_TARGET_BAUDRATE);
    OSAL_DelayMs(100); 
    
    LoRa_Port_SyncAuxState();
    LoRa_Port_ClearRxBuffer();
    
    return cfg_ok;
}
