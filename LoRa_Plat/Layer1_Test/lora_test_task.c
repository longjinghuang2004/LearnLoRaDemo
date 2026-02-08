/**
  ******************************************************************************
  * @file    lora_test_task.c
  * @author  XYY Project Team
  * @brief   Layer 1: LoRa 双机通信测试任务
  *          包含发送端和接收端的逻辑，通过宏 CURRENT_ROLE 切换
  ******************************************************************************
  */

#include "stm32f10x.h"
#include "bsp_uart3_dma.h"
#include "mod_lora.h"
#include "LED.h"
#include "Delay.h"
#include "Serial.h"
#include <string.h>
#include <stdio.h>

// ==========================================
//       角色选择 (烧录前请修改此处)
// ==========================================
// 1 = 发送端 (Board A): 循环发送 "TOGGLE"
// 2 = 接收端 (Board B): 接收指令并控制 LED
#define CURRENT_ROLE    2
// ==========================================

// 统一配置：两块板子都使用地址 1 (或者你可以区分 A=1, B=2，但在透传模式下地址不重要)
#define TARGET_ADDR     0x0001 

/**
  * @brief  LoRa 双机通信测试主函数
  *         在 main.c 中调用此函数，不要返回
  */
void LoRa_Dual_Test_Run(void)
{
    // 1. 硬件初始化
    LED_Init();
    Serial_Init(); // USART1 用于调试打印 (连接电脑)
    
    printf("\r\n=================================================\r\n");
    printf("[SYSTEM] Start. Role: %s\r\n", (CURRENT_ROLE==1)?"SENDER (Tx)":"RECEIVER (Rx)");
    printf("=================================================\r\n");

    // 2. LoRa 模块初始化 (GPIO初始化)
    LoRa_Init();

    // 3. 强制自动配置 (确保两块板子参数绝对一致)
    // 注意：mod_lora.c 中的 LoRa_AutoConfig 必须已修正为 115200 波特率
    LoRa_AutoConfig(TARGET_ADDR);
    
    // 4. 提示配置完成 (LED 闪烁 3 次)
    for(int i=0; i<3; i++) {
        LED1_Turn();
        Delay_ms(100);
    }
    LED1_OFF(); // 默认灭

    printf("[LoRa] Configured. Mode: Trans(115200).\r\n");

    // 5. [关键] 清空接收缓冲区
    // 配置过程中模块可能会输出 "OK" 等回应，或者模式切换产生杂波
    // 我们需要清空这些旧数据，以免干扰主循环
    Delay_ms(100); 
    BSP_UART3_ResetRx(); 
    printf("[LoRa] Rx Buffer Cleared. Loop Started.\r\n");

#if (CURRENT_ROLE == 1)
    // ==========================================
    //           发送端逻辑 (Board A)
    // ==========================================
    int count = 0;
    char send_buf[32];

    while (1)
    {
        // 1. 构造数据
        // 发送纯文本指令，末尾加换行符方便接收端观察，但不是必须的
        sprintf(send_buf, "TOGGLE"); 
        
        // 2. 发送数据 (底层会自动处理 AUX 忙等待)
        BSP_UART3_SendString(send_buf);
        
        // 3. 串口打印调试信息
        printf("[Tx] Sent: %s (Count: %d)\r\n", send_buf, ++count);
        
        // 4. LED 闪烁指示发送动作
        LED1_ON();
        Delay_ms(100);
        LED1_OFF();

        // 5. 延时 1.5 秒 (留给接收端足够的时间处理和回显)
        Delay_ms(1500);
    }

#else
    // ==========================================
    //           接收端逻辑 (Board B)
    // ==========================================
    printf("[Rx] Waiting for 'TOGGLE' command...\r\n");

    while (1)
    {
        // 检查是否有数据包到达 (由 DMA + IDLE 中断置位)
        if (UART3_RxFlag == 1)
        {
            // [安全处理] 手动添加字符串结束符
            // 防止接收到的数据没有 \0 导致 printf 越界崩溃
            if (UART3_RxLength < UART3_RX_BUF_SIZE) {
                UART3_RxBuffer[UART3_RxLength] = '\0';
            } else {
                UART3_RxBuffer[UART3_RX_BUF_SIZE - 1] = '\0';
            }

            // 1. 打印原始数据 (显示长度和内容，方便排查乱码)
            printf("[Rx Data] Len:%d | Content: %s\r\n", UART3_RxLength, UART3_RxBuffer);

            // 2. 解析指令
            // 使用 strstr 查找子串，这样即使数据前后有乱码也能识别
            if (strstr((char*)UART3_RxBuffer, "TOGGLE") != NULL)
            {
                LED1_Turn(); // 翻转 LED
                printf("   >>> Action: LED Toggled! <<<\r\n");
            }
            else if (strstr((char*)UART3_RxBuffer, "ON") != NULL)
            {
                LED1_ON();
                printf("   >>> Action: LED ON <<<\r\n");
            }
            else if (strstr((char*)UART3_RxBuffer, "OFF") != NULL)
            {
                LED1_OFF();
                printf("   >>> Action: LED OFF <<<\r\n");
            }

            // 3. [关键] 清除接收标志，重置 DMA
            // 如果不调用此函数，UART3_RxFlag 永远为 1，且 DMA 不会接收新数据
            BSP_UART3_ResetRx();
        }
    }
#endif
}
