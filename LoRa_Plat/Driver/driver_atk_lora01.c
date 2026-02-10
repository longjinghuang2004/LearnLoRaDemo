#include "lora_driver.h"
#include "lora_port.h"
#include "Delay.h"
#include "Serial.h" 
#include "stm32f10x.h" // 需要直接操作串口寄存器进行救砖
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
    return (uint8_t)power; // ATK 定义正好是 0-3 (0=11dBm)
}

// --- 内部辅助: 临时修改串口波特率 (用于救砖) ---
static void _ReInitUart(uint32_t baudrate) {
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    
    // 重新初始化 USART3
    USART_Init(USART3, &USART_InitStructure);
    USART_Cmd(USART3, ENABLE);
}

// --- 内部辅助: 阻塞式 AT 发送 ---
static bool _SendAT(const char *cmd, const char *expect, uint32_t timeout) {
    Port_ClearRxBuffer(); // 发送前清空接收区
    Port_WriteData((uint8_t*)cmd, strlen(cmd));
    
    uint32_t start = GetTick();
    char rx_buf[128];
    uint16_t rx_idx = 0;
    
    // 简单轮询接收
    while (GetTick() - start < timeout) {
        uint8_t byte;
        if (Port_ReadData(&byte, 1) > 0) {
            rx_buf[rx_idx++] = byte;
            if (rx_idx >= 127) rx_idx = 127;
            rx_buf[rx_idx] = '\0';
            // 检查是否包含期望字符串
            if (strstr(rx_buf, expect)) return true;
        }
    }
    return false;
}

// --- 接口实现 ---

void Drv_Init(void) {
    Port_Init(); // 初始化 MCU 侧 UART/GPIO (默认115200)
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

// --- 核心: 智能配置流程 (增强版) ---
bool Drv_SmartConfig(void) {
    printf("[DRV] Smart Config Start...\r\n");
    
    // 1. 进入配置模式
    Drv_SetMode(1);
    Delay_ms(500); // 给模块充足的时间切换模式
    
    // 2. 握手与波特率自适应 (救砖逻辑)
    bool link_ok = false;
    
    // 尝试 A: 默认 115200
    if (_SendAT("AT\r\n", "OK", 200)) {
        link_ok = true;
        printf("[DRV] Link OK at 115200.\r\n");
    } 
    else {
        // 尝试 B: 切换到 9600 救砖
        printf("[DRV] 115200 Fail. Trying 9600...\r\n");
        _ReInitUart(9600);
        Delay_ms(100);
        
        if (_SendAT("AT\r\n", "OK", 200)) {
            printf("[DRV] Link OK at 9600. Switching module to 115200...\r\n");
            // 发送指令将模块改为 115200 (7=115200, 0=None)
            if (_SendAT("AT+UART=7,0\r\n", "OK", 500)) {
                // 模块改好了，现在把 STM32 也改回 115200
                _ReInitUart(115200);
                Delay_ms(100);
                // 再次确认
                if (_SendAT("AT\r\n", "OK", 200)) link_ok = true;
            }
        }
    }
    
    if (!link_ok) {
        printf("[DRV] Fatal: Module not responding! Check Wiring.\r\n");
        // 恢复默认波特率以免影响后续逻辑
        _ReInitUart(115200); 
        return false;
    }
    
    // 3. 强制写入参数 (根据 Config.h)
    char cmd[64];
    
    // 配置速率与信道: AT+WLRATE=Ch,Rate
    sprintf(cmd, "AT+WLRATE=%d,%d\r\n", DEFAULT_LORA_CHANNEL, _MapRate(DEFAULT_LORA_RATE));
    if (!_SendAT(cmd, "OK", 500)) return false;
    Delay_ms(50);
    
    // 配置功率: AT+TPOWER=Power (关键：这里会写入 0=11dBm)
    sprintf(cmd, "AT+TPOWER=%d\r\n", _MapPower(DEFAULT_LORA_POWER));
    if (!_SendAT(cmd, "OK", 500)) return false;
    Delay_ms(50);
    
    // 配置模式: 透传+一般
    if (!_SendAT("AT+TMODE=0\r\n", "OK", 500)) return false;
    Delay_ms(50);
    if (!_SendAT("AT+CWMODE=0\r\n", "OK", 500)) return false;
    
    // 4. 退出配置模式
    Drv_SetMode(0);
    Delay_ms(500); // 等待模块重启
    
    // 等待 AUX 变低 (模块初始化完成)
    uint32_t wait_start = GetTick();
    while (Drv_IsBusy()) {
        if (GetTick() - wait_start > 2000) {
            printf("[DRV] Warning: AUX stuck High (Busy/Error)!\r\n");
            break;
        }
    }
    
    printf("[DRV] Config Success!\r\n");
    return true;
}
