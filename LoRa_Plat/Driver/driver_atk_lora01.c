#include "lora_driver.h"
#include "lora_port.h"
#include "Delay.h"
#include "Serial.h" 
#include "stm32f10x.h"
#include <stdio.h>
#include <string.h>

// --- 内部辅助: 参数映射 ---
static uint8_t _MapRate(LoRa_AirRate_t rate) {
    switch(rate) {
        case LORA_RATE_0K3:  return 0;
        case LORA_RATE_1K2:  return 1;
        case LORA_RATE_2K4:  return 2;
        case LORA_RATE_4K8:  return 3;
        case LORA_RATE_9K6:  return 4;
        case LORA_RATE_19K2: return 5;
        default: return 5;
    }
}

static uint8_t _MapPower(LoRa_Power_t power) {
    return (uint8_t)power; 
}

// --- 内部辅助: 临时修改串口波特率 ---
static void _ReInitUart(uint32_t baudrate) {
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStructure);
    USART_Cmd(USART3, ENABLE);
}

// --- 内部辅助: 阻塞式 AT 发送 ---
static bool _SendAT(const char *cmd, const char *expect, uint32_t timeout) {
    Port_ClearRxBuffer(); // 发送前清空
    Port_WriteData((uint8_t*)cmd, strlen(cmd));
    
    uint32_t start = GetTick();
    char rx_buf[128];
    uint16_t rx_idx = 0;
    
    while (GetTick() - start < timeout) {
        uint8_t byte;
        if (Port_ReadData(&byte, 1) > 0) {
            rx_buf[rx_idx++] = byte;
            if (rx_idx >= 127) rx_idx = 127;
            rx_buf[rx_idx] = '\0';
            if (strstr(rx_buf, expect)) {
                Delay_ms(10); 
                Port_ClearRxBuffer();
                return true;
            }
        }
    }
    return false;
}

// --- 接口实现 ---

void Drv_Init(void) {
    Port_Init(); 
}

void Drv_SetMode(uint8_t mode) {
    Port_SetMD0(mode == 1);
}

bool Drv_IsBusy(void) {
    return Port_GetAUX();
}

uint16_t Drv_Write(const uint8_t *data, uint16_t len) {
    return Port_WriteData(data, len);
}

uint16_t Drv_Read(uint8_t *buf, uint16_t max_len) {
    return Port_ReadData(buf, max_len);
}

void Drv_HardReset(void) {
    Port_SetRST(false);
    Delay_ms(10);
    Port_SetRST(true);
    Delay_ms(100);
}

// 救砖配置：始终恢复到最保守的状态 (透传, 地址0)
bool Drv_SmartConfig(void) {
    printf("[DRV] Smart Config (Factory Reset) Start...\r\n");
    
    Drv_SetMode(1);
    Delay_ms(600); 
    
    bool link_ok = false;
    
    // 尝试 115200
    if (_SendAT("AT\r\n", "OK", 200)) {
        link_ok = true;
    } else {
        // 尝试 9600
        _ReInitUart(9600);
        Delay_ms(100);
        if (_SendAT("AT\r\n", "OK", 200)) {
            if (_SendAT("AT+UART=7,0\r\n", "OK", 500)) {
                _ReInitUart(115200);
                Delay_ms(100);
                if (_SendAT("AT\r\n", "OK", 200)) link_ok = true;
            }
        }
    }
    
    if (!link_ok) {
        printf("[DRV] Fatal: Module not responding!\r\n");
        _ReInitUart(115200); 
        return false;
    }
    
    // 强制恢复默认
    char cmd[64];
    _SendAT("AT+ADDR=00,00\r\n", "OK", 500); Delay_ms(50);
    sprintf(cmd, "AT+WLRATE=%d,%d\r\n", DEFAULT_LORA_CHANNEL, _MapRate(DEFAULT_LORA_RATE));
    _SendAT(cmd, "OK", 500); Delay_ms(50);
    sprintf(cmd, "AT+TPOWER=%d\r\n", _MapPower(DEFAULT_LORA_POWER));
    _SendAT(cmd, "OK", 500); Delay_ms(50);
    _SendAT("AT+TMODE=0\r\n", "OK", 500); Delay_ms(50); // 强制透传
    _SendAT("AT+CWMODE=0\r\n", "OK", 500);
    
    Drv_SetMode(0);
    Delay_ms(600); 
    while (Drv_IsBusy()); 
    
    printf("[DRV] Factory Reset Success!\r\n");
    return true;
}

// 应用用户配置
bool Drv_ApplyConfig(const LoRa_Config_t *cfg) {
    printf("[DRV] Applying: Addr=0x%04X, Ch=%d, Rate=%d, Pwr=%d, Mode=%d\r\n", 
           cfg->addr, cfg->channel, cfg->air_rate, cfg->power, cfg->tmode);
    
    Drv_SetMode(1);
    Delay_ms(600); 
    
    char cmd[64];
    bool success = true;

    // 1. 设置地址 (支持用户自定义)
    sprintf(cmd, "AT+ADDR=%02X,%02X\r\n", (cfg->addr >> 8) & 0xFF, cfg->addr & 0xFF);
    if (!_SendAT(cmd, "OK", 500)) success = false;
    Delay_ms(50);

    // 2. 设置速率信道
    sprintf(cmd, "AT+WLRATE=%d,%d\r\n", cfg->channel, cfg->air_rate);
    if (!_SendAT(cmd, "OK", 500)) success = false;
    Delay_ms(50);

    // 3. 设置功率
    sprintf(cmd, "AT+TPOWER=%d\r\n", cfg->power);
    if (!_SendAT(cmd, "OK", 500)) success = false;
    Delay_ms(50);

    // 4. 设置传输模式 (透传/定向)
    sprintf(cmd, "AT+TMODE=%d\r\n", cfg->tmode);
    if (!_SendAT(cmd, "OK", 500)) success = false;
    Delay_ms(50);

    Drv_SetMode(0);
    Delay_ms(600); 
    while (Drv_IsBusy()); 

    Port_ClearRxBuffer();

    if (success) printf("[DRV] Apply Success!\r\n");
    else printf("[DRV] Apply Failed!\r\n");
    
    return success;
}