#include "stm32f10x.h"
#include "bsp_uart3_dma.h"
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
    #define TARGET_ADDR     0x0001 // 发送端保持原地址
#else
    #define TARGET_ADDR     0xFFFF // 接收端改为广播监听地址
#endif


#if (CURRENT_ROLE == 2)
#define APP_RX_BUF_SIZE 256
uint8_t g_app_rx_buffer[APP_RX_BUF_SIZE];
uint16_t g_app_rx_len = 0;
void Parse_LoRa_Packet(void);
#endif

static uint8_t Calculate_Checksum(const uint8_t* data, uint8_t len)
{
    uint8_t sum = 0;
    for(uint8_t i = 0; i < len; i++) sum += data[i];
    return sum;
}

void LoRa_Dual_Test_Run(void)
{
    // 1. 基础初始化
    SysTick_Init(); // [新增] 初始化系统滴答
    LED_Init();
    Serial_Init();
    LoRa_Init();
    
    printf("\r\n[SYSTEM] Booting... Role: %s\r\n", (CURRENT_ROLE==1)?"SENDER":"RECEIVER");

    // 2. 执行 LoRa 自检与配置
    // 如果自检失败，死循环报错
    if (!LoRa_Init_And_SelfCheck(TARGET_ADDR))
    {
        printf("[SYSTEM] LoRa Init Failed! System Halted.\r\n");
        while(1) {
            // 快速闪烁 LED 报错
            LED1_Turn(); Delay_ms(100);
        }
    }

    // 自检通过，进入主循环
    printf("[SYSTEM] LoRa Init Success. Entering Main Loop.\r\n");

#if (CURRENT_ROLE == 1)
    // ==========================================
    //           发送端逻辑 (Board A)
    // ==========================================
    int count = 0;
    uint8_t send_buffer[64]; 
    char command_str[32]; // 稍微加大一点

    while (1)
    {
        // 1. 构造动态数据：包含计数器，方便检测丢包
        sprintf(command_str, "PING:%d", count);
        
        uint8_t *p_buf = send_buffer;
        uint8_t cmd_len = strlen(command_str);
        uint16_t total_len = 0;

        // --- 协议封装 ---
        *p_buf++ = LORA_PACKET_HEADER_0;
        *p_buf++ = LORA_PACKET_HEADER_1;
        *p_buf++ = cmd_len;
        memcpy(p_buf, command_str, cmd_len);
        p_buf += cmd_len;
        *p_buf++ = Calculate_Checksum((uint8_t*)command_str, cmd_len);
        *p_buf++ = LORA_PACKET_TAIL_0;
        *p_buf++ = LORA_PACKET_TAIL_1;
        
        total_len = p_buf - send_buffer;

        // 2. 发送
        BSP_UART3_SendBytes(send_buffer, total_len);
        
        printf("[Tx] Sent: %s (Len:%d)\r\n", command_str, total_len);
        
        // 3. 状态指示
        LED1_ON();
        Delay_ms(50);
        LED1_OFF();

        // 4. 延时 (建议先设为 2秒，给接收端充足的处理和打印时间)
        Delay_ms(2000);
        
        count++;
    }



#else
    // ==========================================
    //           接收端逻辑 (Board B)
    // ==========================================
    printf("[Rx] Waiting for LoRa packets...\r\n");

    while (1)
    {

        // 1. DMA数据搬运 (保持不变)
        if (UART3_RxFlag == 1)
        {
					
            // 打印底层接收到的原始字节数，用于诊断分包情况
            // printf("[DMA] Recv %d bytes\r\n", UART3_RxLength); 
//    printf("[Debug] Raw Hex: ");
//    for(int k=0; k<UART3_RxLength; k++) printf("%02X ", UART3_RxBuffer[k]);
//    printf("\r\n");
					
            if (g_app_rx_len + UART3_RxLength < APP_RX_BUF_SIZE)
            {
                memcpy(&g_app_rx_buffer[g_app_rx_len], UART3_RxBuffer, UART3_RxLength);
                g_app_rx_len += UART3_RxLength;
            }
            else
            {
                g_app_rx_len = 0;
                printf("[Error] Buffer Overflow! Reset.\r\n");
            }
            BSP_UART3_ResetRx();
        }

        // 2. 协议解析
        if (g_app_rx_len > 0)
        {
            Parse_LoRa_Packet();
        }
    }
#endif

}

#if (CURRENT_ROLE == 2)
void Parse_LoRa_Packet(void)
{
    uint16_t i = 0;
    
    while (i < g_app_rx_len)
    {
        // 1. 寻找包头
        if (g_app_rx_buffer[i] == LORA_PACKET_HEADER_0 && 
            (i + 1 < g_app_rx_len) && 
            g_app_rx_buffer[i+1] == LORA_PACKET_HEADER_1)
        {
            // 找到包头
            if (i + 2 < g_app_rx_len)
            {
                uint8_t cmd_len = g_app_rx_buffer[i + 2];
                
                // 简单的长度合理性检查
                if (cmd_len == 0 || cmd_len > MAX_CMD_LENGTH) {
                    printf("[Rx Err] Invalid Len: %d. Skip header.\r\n", cmd_len);
                    i++; continue;
                }
                
                uint16_t packet_total_len = 2 + 1 + cmd_len + 1 + 2;

                if (i + packet_total_len <= g_app_rx_len)
                {
                    uint8_t* packet_start = &g_app_rx_buffer[i];
                    
                    // 校验
                    uint8_t calc_sum = Calculate_Checksum(packet_start + 3, cmd_len);
                    uint8_t recv_sum = packet_start[3 + cmd_len];
                    
                    if (recv_sum == calc_sum &&
                        packet_start[packet_total_len - 2] == LORA_PACKET_TAIL_0 &&
                        packet_start[packet_total_len - 1] == LORA_PACKET_TAIL_1)
                    {
                        // --- 成功接收 ---
                        char cmd_buffer[MAX_CMD_LENGTH + 1] = {0};
                        memcpy(cmd_buffer, packet_start + 3, cmd_len);
                        
                        printf("[Rx OK] Payload: %s\r\n", cmd_buffer);
                        
                        // 业务逻辑处理
                        if (strncmp(cmd_buffer, "PING:", 5) == 0) {
                            LED1_Turn(); // 收到PING就闪灯
                        }

                        // 移除已处理数据
                        uint16_t processed_len = i + packet_total_len;
                        g_app_rx_len -= processed_len;
                        memmove(g_app_rx_buffer, &g_app_rx_buffer[processed_len], g_app_rx_len);
                        i = 0; 
                        continue; 
                    }
                    else {
                        printf("[Rx Err] Checksum/Tail Fail. CalcSum:%02X, RecvSum:%02X\r\n", calc_sum, recv_sum);
                        i++; // 校验失败，跳过这个“伪包头”
                    }
                }
                else {
                    // 包不完整，等待更多数据
                    break; 
                }
            } else {
                // 长度字段还没收到，等待
                break;
            }
        } else {
            i++; // 不是包头，继续找
        }
    }
    
    // 清理无效数据
    if (i > 0 && i >= g_app_rx_len) {
        g_app_rx_len = 0;
    } else if (i > 0) {
        g_app_rx_len -= i;
        memmove(g_app_rx_buffer, &g_app_rx_buffer[i], g_app_rx_len);
    }
}

#endif
