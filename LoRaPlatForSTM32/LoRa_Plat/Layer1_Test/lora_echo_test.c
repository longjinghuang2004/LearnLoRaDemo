/**
  ******************************************************************************
  * @file    lora_echo_test.c
  * @author  XYY Project Team
  * @brief   Layer 1: LoRa 回响测试 (带 LED 诊断版)
  ******************************************************************************
  */

#include "lora_echo_test.h"
#include "mod_lora.h"       
#include "bsp_lora_uart.h"  
#include "Serial.h"         
#include "Delay.h"
#include "LED.h"
#include <string.h>
#include <stdio.h>

// --- 全局对象 ---
static LoRa_Dev_t g_LoRaDev;
static uint8_t g_RxBuf[256]; 

// --- 硬件适配 ---
static void _App_GPIO_Config(void) {
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4; // MD0
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5; // AUX
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
}
static uint16_t _App_Send(const uint8_t *d, uint16_t l) { return BSP_LoRa_UART_Send(d, l); }
static uint16_t _App_Read(uint8_t *b, uint16_t l)       { return BSP_LoRa_UART_Read(b, l); }
static void     _App_SetMD0(uint8_t l)                  { GPIO_WriteBit(GPIOA, GPIO_Pin_4, (l)?Bit_SET:Bit_RESET); }
static uint8_t  _App_ReadAUX(void)                      { return GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_5); }

// --- 初始化逻辑 ---
static bool LoRa_System_Init(void) {
    _App_GPIO_Config();
    LoRa_Driver_t drv = { .Send = _App_Send, .Read = _App_Read, .SetMD0 = _App_SetMD0, .ReadAUX = _App_ReadAUX, .DelayMs = Delay_ms, .GetTick = GetTick };
    LoRa_Init(&g_LoRaDev, &drv);

    printf("[Init] Auto-Baud Detection...\r\n");
    BSP_LoRa_UART_Init(); 
    if (!LoRa_SendAT(&g_LoRaDev, "AT\r\n", NULL, 0, "OK", 200)) {
        // 尝试 9600 救砖
        USART_InitTypeDef USART_InitStructure;
        USART_InitStructure.USART_BaudRate = 9600;
        USART_InitStructure.USART_WordLength = USART_WordLength_8b;
        USART_InitStructure.USART_StopBits = USART_StopBits_1;
        USART_InitStructure.USART_Parity = USART_Parity_No;
        USART_InitStructure.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
        USART_InitStructure.USART_Mode = USART_Mode_Rx | USART_Mode_Tx;
        USART_Init(USART3, &USART_InitStructure);
        if (LoRa_SendAT(&g_LoRaDev, "AT\r\n", NULL, 0, "OK", 200)) {
            LoRa_SendAT(&g_LoRaDev, "AT+UART=7,0\r\n", NULL, 0, "OK", 500);
            Delay_ms(100);
            BSP_LoRa_UART_Init(); 
        } else return false;
    }
    printf("  -> Link Established at 115200.\r\n");

    // 参数配置
    LoRa_SendAT(&g_LoRaDev, "AT+WLRATE=23,5\r\n", NULL, 0, "OK", 500); 
    LoRa_SendAT(&g_LoRaDev, "AT+TMODE=0\r\n", NULL, 0, "OK", 500);     
    LoRa_SendAT(&g_LoRaDev, "AT+CWMODE=0\r\n", NULL, 0, "OK", 500);    
    
    // [关键] 将发射功率调至最低 (0=11dBm)，防止近距离饱和
    LoRa_SendAT(&g_LoRaDev, "AT+TPOWER=0\r\n", NULL, 0, "OK", 500);

    LoRa_SetPacketHeader(&g_LoRaDev, 'C', 'M');
    LoRa_SetPacketTail(&g_LoRaDev, '\n', '\n');
    return true;
}

// ====================================================
//    主运行逻辑
// ====================================================
void LoRa_Echo_Test_Run(void)
{
    if (!LoRa_System_Init()) {
        printf("[Error] LoRa Init Failed.\r\n");
        while(1) { LED1_ON(); Delay_ms(100); LED1_OFF(); Delay_ms(100); }
    }

#if LORA_ROLE_HOST
    // ------------------------------------------------
    //                 主机逻辑 (HOST)
    // ------------------------------------------------
    printf("\r\n[Mode] HOST (Sender)\r\n");
    printf("Ready. Type string in Serial Assistant.\r\n");

    while (1)
    {
        if (Serial_RxFlag == 1)
        {
            char *input = Serial_RxPacket;
            uint16_t len = strlen(input);
            while(len > 0 && (input[len-1]=='\n' || input[len-1]=='\r')) input[--len] = '\0';

            if (len > 0)
            {
                printf("\r\n[Host] Sending: \"%s\"\r\n", input);
                
                // 发送前清空接收缓冲区，防止旧数据干扰
                BSP_LoRa_UART_ClearRx();
                
                LED1_ON();
                LoRa_SendPacket(&g_LoRaDev, (uint8_t*)input, len);
                LED1_OFF();
                
                printf("[Host] Waiting for Echo...\r\n");
                
                uint32_t start = GetTick();
                bool received = false;
                
                while (GetTick() - start < HOST_WAIT_TIMEOUT)
                {
                    uint16_t rx_len = LoRa_ReceivePacket(&g_LoRaDev, g_RxBuf, sizeof(g_RxBuf));
                    if (rx_len > 0)
                    {
                        g_RxBuf[rx_len] = '\0';
                        printf("[Host] <== Echo from Slave: \"%s\"\r\n", g_RxBuf);
                        received = true;
                        break;
                    }
                }
                if (!received) printf("[Host] Timeout! No response.\r\n");
            }
            Serial_RxFlag = 0;
        }
    }

#else
    // ------------------------------------------------
    //                 从机逻辑 (SLAVE)
    // ------------------------------------------------
    // 启动时闪烁3次，表示从机就绪
    for(int i=0; i<3; i++) { LED1_ON(); Delay_ms(100); LED1_OFF(); Delay_ms(100); }
    
    // 这里的 printf 只有在从机接了串口线时才看得到，否则是盲跑
    printf("\r\n[Mode] SLAVE (Receiver)\r\n");

    while (1)
    {
        uint16_t rx_len = LoRa_ReceivePacket(&g_LoRaDev, g_RxBuf, sizeof(g_RxBuf));
        
        if (rx_len > 0)
        {
            // --- 诊断步骤 1: 接收成功 ---
            // 快闪2次，表示“我收到了！”
            LED1_ON(); Delay_ms(50); LED1_OFF(); Delay_ms(50);
            LED1_ON(); Delay_ms(50); LED1_OFF();
            
            g_RxBuf[rx_len] = '\0';
            printf("[Slave] Rx: \"%s\"\r\n", g_RxBuf);
            
            // --- 诊断步骤 2: 延时等待 ---
            // [修改] 增加延时到 500ms，确保主机有足够时间切换 RX
            Delay_ms(500); 
            
            // --- 诊断步骤 3: 开始回传 ---
            // 长亮，表示“我正在发！”
            LED1_ON();
            LoRa_SendPacket(&g_LoRaDev, g_RxBuf, rx_len);
            Delay_ms(100); // 保持亮灯一会让人眼能看清
            LED1_OFF();
            
            printf("[Slave] Echo sent.\r\n");
        }
    }
#endif
}
