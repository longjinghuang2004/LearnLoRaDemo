/**
  * @file    lora_test_task.c
  * @brief   Layer 1: LoRa 模块/串口驱动 测试任务
  */

#include "stm32f10x.h"
#include "bsp_uart3_dma.h"
#include "LED.h"
#include "Delay.h"
#include "Serial.h" // 用于 printf 打印调试信息到 PC (USART1)

/**
  * @brief  测试入口函数
  *         请在 main.c 的 while(1) 之前调用此函数，它包含死循环
  */
void LoRa_Test_Run(void)
{
    printf("\r\n[TEST] Start UART3 Loopback Test...\r\n");
    printf("[TEST] Connect USB-TTL to PB10(TX) and PB11(RX)\r\n");
    printf("[TEST] Baudrate: 115200\r\n");

    // 1. 初始化 Layer 3 驱动
    BSP_UART3_Init(115200);
    
    // 2. 发送欢迎语
    BSP_UART3_SendString("Hello from STM32 UART3!\r\n");

    while (1)
    {
        // 检查是否收到数据
        if (UART3_RxFlag == 1)
        {
            LED1_Turn(); // 翻转 LED 表示收到数据

            // 1. 在调试串口 (USART1) 打印收到的信息
            printf("[UART3 Rx] Len=%d, Data: %s\r\n", UART3_RxLength, UART3_RxBuffer);

            // 2. 回显：把收到的数据通过 UART3 发回去
            BSP_UART3_SendString("Echo: ");
            BSP_UART3_SendBytes(UART3_RxBuffer, UART3_RxLength);
            BSP_UART3_SendString("\r\n");

            // 3. 清空缓冲区并重置 DMA，准备下一次接收
            // 注意：必须手动清空 Buffer 里的字符串结束符，否则 printf 可能会打印乱码
            for(int i=0; i<UART3_RxLength; i++) UART3_RxBuffer[i] = 0;
            
            BSP_UART3_ResetRx();
        }
    }
}
