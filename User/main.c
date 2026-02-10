#include "stm32f10x.h"
#include "Delay.h"
#include "Serial.h"
#include "LED.h"
#include "lora_app.h"
#include "lora_port.h"
#include <stdio.h>
#include <string.h>

volatile uint8_t g_TimeoutFlag;

// ============================================================
//                    本地辅助函数
// ============================================================

static void ReInit_UART3(uint32_t baudrate)
{
    USART_InitTypeDef USART_InitStructure;
    USART_InitStructure.USART_BaudRate = baudrate;
    USART_InitStructure.USART_WordLength = USART_WordLength_8b;
    USART_InitStructure.USART_StopBits = USART_StopBits_1;
    USART_InitStructure.USART_Parity = USART_Parity_No;
    USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
    USART_Init(USART3, &USART_InitStructure);
}

/**
 * @brief 阻塞式发送 AT 指令并检查响应 (带Debug打印)
 */
static bool SendAT_Blocking(const char *cmd, const char *expect, uint32_t timeout_ms)
{
    // 1. 清空接收缓冲区
    Port_ClearRxBuffer();
    
    // 2. 发送指令
    Port_WriteData((uint8_t*)cmd, strlen(cmd));
    
    // 3. 循环接收
    uint32_t start = GetTick();
    char rx_buf[128];
    uint16_t rx_idx = 0;
    
    memset(rx_buf, 0, sizeof(rx_buf));
    
    while (GetTick() - start < timeout_ms)
    {
        uint8_t byte;
        if (Port_ReadData(&byte, 1) > 0)
        {
            rx_buf[rx_idx++] = byte;
            if (rx_idx >= sizeof(rx_buf) - 1) rx_idx = sizeof(rx_buf) - 1;
            rx_buf[rx_idx] = '\0'; 
            
            // 检查匹配
            if (strstr(rx_buf, expect) != NULL)
            {
                return true; 
            }
        }
    }
    
    // --- 失败时打印缓冲区内容，帮助定位问题 ---
    printf("[INIT] AT Fail! Cmd: %s", cmd); // cmd 自带 \r\n
    printf("[INIT] Rx Buffer Content: ");
    for(int i=0; i<rx_idx; i++) {
        // 打印可见字符，不可见字符转义
        if(rx_buf[i] >= 32 && rx_buf[i] <= 126) printf("%c", rx_buf[i]);
        else if(rx_buf[i] == '\r') printf("\\r");
        else if(rx_buf[i] == '\n') printf("\\n");
        else printf("[%02X]", rx_buf[i]);
    }
    printf("\r\n");
    
    return false; 
}

/**
 * @brief LoRa 模块自适应初始化流程 (增强版)
 */
static bool LoRa_Hardware_Setup(void)
{
    printf("[INIT] Entering Config Mode (MD0=1)...\r\n");
    
    Port_SetMD0(true); 
    Delay_ms(200); // [修改] 增加进入配置模式后的等待时间
    
    // 等待 AUX 变低
    uint32_t timeout = 2000;
    while (Port_GetAUX() && timeout--) Delay_ms(1);
    if (timeout == 0) {
        printf("[INIT] Error: AUX Timeout (Module not ready)\r\n");
        return false;
    }

    bool link_ok = false;

    // --- 步骤 1: 波特率握手 ---
    printf("[INIT] Trying 115200 baud...\r\n");
    if (SendAT_Blocking("AT\r\n", "OK", 500))
    {
        printf("[INIT] -> OK. Module is at 115200.\r\n");
        link_ok = true;
    }
    else
    {
        printf("[INIT] -> No response. Trying 9600 baud...\r\n");
        ReInit_UART3(9600);
        Delay_ms(100);
        
        if (SendAT_Blocking("AT\r\n", "OK", 500))
        {
            printf("[INIT] -> OK. Module is at 9600. Switching to 115200...\r\n");
            if (SendAT_Blocking("AT+UART=7,0\r\n", "OK", 1000))
            {
                ReInit_UART3(115200);
                Delay_ms(200); // [修改] 切波特率后多等一会
                if (SendAT_Blocking("AT\r\n", "OK", 500)) {
                    printf("[INIT] -> Sync Success at 115200.\r\n");
                    link_ok = true;
                }
            }
        }
    }

    if (!link_ok) return false;

    // --- 步骤 2: 参数配置 (带重试和延时) ---
    printf("[INIT] Configuring Parameters...\r\n");
    
    // [修改] 每次发送前增加延时，防止指令过快
    Delay_ms(100); 
    if (!SendAT_Blocking("AT+WLRATE=23,5\r\n", "OK", 1000)) {
        printf("[INIT] Retry WLRATE...\r\n");
        Delay_ms(500);
        if (!SendAT_Blocking("AT+WLRATE=23,5\r\n", "OK", 1000)) return false;
    }

    Delay_ms(100);
    if (!SendAT_Blocking("AT+TMODE=0\r\n", "OK", 1000)) return false;

    Delay_ms(100);
    if (!SendAT_Blocking("AT+CWMODE=0\r\n", "OK", 1000)) return false;

    // --- 步骤 3: 退出配置 ---
    printf("[INIT] Exiting Config Mode (MD0=0)...\r\n");
    Port_SetMD0(false);
    Delay_ms(100);
    while (Port_GetAUX()); 

    return true;
}

// ============================================================
//                    主函数
// ============================================================

int main(void)
{
    SysTick_Init();
    LED_Init();
    Serial_Init();
    LED1_OFF();
    
    printf("\r\n=========================================\r\n");
    printf("      LoRa System Boot (Robust Init)     \r\n");
    printf("=========================================\r\n");
    
    Port_Init();
    
    if (LoRa_Hardware_Setup())
    {
        printf("[INIT] Hardware Setup SUCCESS!\r\n");
        for (int i = 0; i < 3; i++) {
            LED1_ON(); Delay_ms(100);
            LED1_OFF(); Delay_ms(100);
        }
        LED1_ON(); 
    }
    else
    {
        printf("[INIT] Hardware Setup FAILED! System Halted.\r\n");
        LED1_OFF();
        while (1);
    }
    
    LoRa_App_Init();
    
    printf("[SYS] Entering Main Loop...\r\n");
    
    while (1)
    {
        LoRa_App_Task();
    }
}
