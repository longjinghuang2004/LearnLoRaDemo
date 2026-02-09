#include "stm32f10x.h"
#include "bsp_lora_uart.h"
#include "mod_lora.h"
#include "LED.h"
#include "Delay.h"
#include "Serial.h"
#include <string.h>
#include <stdio.h>
#include "lora_protocol.h"

// ==========================================
//       角色选择 (烧录前请修改此处)
// ==========================================
#define CURRENT_ROLE    2
// ==========================================

#if (CURRENT_ROLE == 1)
    #define TARGET_ADDR     0x0001 
#else
    #define TARGET_ADDR     0xFFFF 
#endif

#if (CURRENT_ROLE == 2)
#define APP_RX_BUF_SIZE 512 
uint8_t g_app_rx_buffer[APP_RX_BUF_SIZE];
uint16_t g_app_rx_len = 0;
void Parse_LoRa_Command(void);
#endif

void LoRa_Dual_Test_Run(void)
{
    // 1. 基础初始化
    SysTick_Init(); 
    LED_Init();
    Serial_Init();
    LoRa_Init();
    
    printf("\r\n[SYSTEM] Booting... Role: %s\r\n", (CURRENT_ROLE==1)?"SENDER":"RECEIVER");

    // 2. 执行 LoRa 自检与配置
    if (!LoRa_Init_And_SelfCheck(TARGET_ADDR))
    {
        printf("[SYSTEM] LoRa Init Failed! System Halted.\r\n");
        while(1) {
            LED1_Turn(); Delay_ms(100);
        }
    }

    printf("[SYSTEM] LoRa Init Success. Entering Main Loop.\r\n");

#if (CURRENT_ROLE == 1)
    // ==========================================
    //           发送端逻辑 (Board A)
    // ==========================================
    uint8_t toggle_state = 0;
    char send_buffer[64]; 

    while (1)
    {
        // 1. 构造文本指令包: Header + Command + Tail
        // 格式: CMLED_on\n\n 或 CMLED_off\n\n
        if (toggle_state == 0)
        {
            sprintf(send_buffer, "%c%c%s%c%c", 
                    LORA_CMD_HEADER_0, LORA_CMD_HEADER_1, 
                    CMD_STR_LED_ON, 
                    LORA_CMD_TAIL_0, LORA_CMD_TAIL_1);
            toggle_state = 1;
        }
        else
        {
            sprintf(send_buffer, "%c%c%s%c%c", 
                    LORA_CMD_HEADER_0, LORA_CMD_HEADER_1, 
                    CMD_STR_LED_OFF, 
                    LORA_CMD_TAIL_0, LORA_CMD_TAIL_1);
            toggle_state = 0;
        }

        // 2. 发送
        BSP_UART3_SendString(send_buffer);
        
        printf("[Tx] Sent: %s", send_buffer); // \n\n 已经在buffer里了，所以这里不用加 \r\n
        
        // 3. 状态指示 (发送时闪一下)
        LED1_ON();
        Delay_ms(50);
        LED1_OFF();

        // 4. 延时 2秒
        Delay_ms(2000);
    }

#else
    // ==========================================
    //           接收端逻辑 (Board B)
    // ==========================================
    printf("[Rx] Waiting for LoRa commands (CM...\\n\\n)...\r\n");
    
    uint8_t temp_dma_buf[128]; 

    while (1)
    {
        // 1. 从 DMA 循环缓冲区读取数据
        uint16_t len = BSP_UART3_ReadRxBuffer(temp_dma_buf, sizeof(temp_dma_buf));
        
        if (len > 0)
        {
            // 搬运到应用层解析缓冲区
            if (g_app_rx_len + len < APP_RX_BUF_SIZE)
            {
                memcpy(&g_app_rx_buffer[g_app_rx_len], temp_dma_buf, len);
                g_app_rx_len += len;
            }
            else
            {
                g_app_rx_len = 0; // 溢出复位
                printf("[Error] App Buffer Overflow!\r\n");
            }
        }
					
        // 2. 协议解析
        if (g_app_rx_len > 0)
        {
            Parse_LoRa_Command();
        }
    }
#endif

}

#if (CURRENT_ROLE == 2)
void Parse_LoRa_Command(void)
{
    uint16_t i = 0;
    uint16_t processed_len = 0;
    
    // 至少需要4个字节才能构成最小包 (CM\n\n)
    while (i + 4 <= g_app_rx_len)
    {
        // 1. 寻找包头 "CM"
        if (g_app_rx_buffer[i] == LORA_CMD_HEADER_0 && 
            g_app_rx_buffer[i+1] == LORA_CMD_HEADER_1)
        {
            // 找到包头，现在向后寻找包尾 "\n\n"
            // 从包头后面开始找 (i+2)
            int tail_index = -1;
            for (int j = i + 2; j < g_app_rx_len - 1; j++)
            {
                if (g_app_rx_buffer[j] == LORA_CMD_TAIL_0 && 
                    g_app_rx_buffer[j+1] == LORA_CMD_TAIL_1)
                {
                    tail_index = j;
                    break;
                }
            }

            if (tail_index != -1)
            {
                // --- 找到完整数据包 ---
                // 包头位置: i
                // 包尾位置: tail_index
                // 指令起始: i + 2
                // 指令长度: tail_index - (i + 2)
                
                uint16_t cmd_start_idx = i + 2;
                uint16_t cmd_len = tail_index - cmd_start_idx;
                
                // 提取指令字符串 (为了安全，拷贝到临时buffer并添加结束符)
                char cmd_str[32] = {0};
                if (cmd_len < 32)
                {
                    memcpy(cmd_str, &g_app_rx_buffer[cmd_start_idx], cmd_len);
                    cmd_str[cmd_len] = '\0'; // 确保是字符串
                    
                    printf("[Rx CMD] Got: %s\r\n", cmd_str);
                    
                    // --- 执行动作 ---
                    if (strcmp(cmd_str, CMD_STR_LED_ON) == 0)
                    {
                        printf("  -> Action: LED ON\r\n");
                        LED1_ON();
                    }
                    else if (strcmp(cmd_str, CMD_STR_LED_OFF) == 0)
                    {
                        printf("  -> Action: LED OFF\r\n");
                        LED1_OFF();
                    }
                    else
                    {
                        printf("  -> Unknown Command\r\n");
                    }
                }
                
                // 标记处理完的位置 (包尾的后一位 + 1)
                // tail_index 是第一个\n, tail_index+1 是第二个\n
                // 所以下一包开始位置是 tail_index + 2
                i = tail_index + 2;
                processed_len = i;
                continue; // 继续寻找缓冲区里是否还有下一个包
            }
            else
            {
                // 找到了头，但没找到尾，说明包还没收全
                // 退出循环，等待下一次 DMA 搬运更多数据进来
                break; 
            }
        }
        else
        {
            // 不是包头，移动一位继续找
            i++;
            processed_len = i;
        }
    }
    
    // 3. 移除已处理的数据 (滑动窗口)
    if (processed_len > 0)
    {
        if (processed_len < g_app_rx_len)
        {
            // 把剩下的数据搬到最前面
            memmove(g_app_rx_buffer, &g_app_rx_buffer[processed_len], g_app_rx_len - processed_len);
            g_app_rx_len -= processed_len;
        }
        else
        {
            // 全部处理完了
            g_app_rx_len = 0;
        }
    }
}
#endif
