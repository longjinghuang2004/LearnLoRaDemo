#include "mod_lora.h"
#include "bsp_uart3_dma.h"
#include "Delay.h"
#include "Serial.h" 
#include <stdio.h>
#include <string.h>

// --- 引脚定义 ---
#define LORA_MD0_PIN    GPIO_Pin_4
#define LORA_MD0_PORT   GPIOA
#define LORA_AUX_PIN    GPIO_Pin_5
#define LORA_AUX_PORT   GPIOA

static void LoRa_GPIO_Init(void)
{
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef GPIO_InitStructure;
    GPIO_InitStructure.GPIO_Pin = LORA_MD0_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(LORA_MD0_PORT, &GPIO_InitStructure);

    GPIO_InitStructure.GPIO_Pin = LORA_AUX_PIN;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(LORA_AUX_PORT, &GPIO_InitStructure);
}

static void LoRa_WaitAux(void)
{
    while (GPIO_ReadInputDataBit(LORA_AUX_PORT, LORA_AUX_PIN) == 1)
    {
        Delay_ms(10);
    }
}

void LoRa_Init(void)
{
    LoRa_GPIO_Init();
}

void LoRa_SetMode_Config(void)
{
    GPIO_SetBits(LORA_MD0_PORT, LORA_MD0_PIN);
    Delay_ms(50); 
    LoRa_WaitAux();
}

void LoRa_SetMode_Trans(void)
{
    GPIO_ResetBits(LORA_MD0_PORT, LORA_MD0_PIN);
    Delay_ms(50);
    LoRa_WaitAux();
}

/**
 * @brief  [重写] 发送AT指令并等待响应 (支持分包拼接)
 * @param  cmd: AT指令
 * @param  expect: 期望的响应子串
 * @param  timeout_ms: 超时时间
 */
static bool LoRa_SendCmd_WaitResp(char* cmd, char* expect, uint32_t timeout_ms)
{
    // 定义一个局部的大缓冲区，用于拼接多次DMA接收的数据
    char rx_accumulator[512] = {0}; 
    uint16_t acc_len = 0;

    // 1. 清空底层DMA缓冲区
    BSP_UART3_ClearRxBuffer();
    
    // 2. 发送指令
    BSP_UART3_SendString(cmd);
    
    // 3. 循环等待
    uint32_t start_time = GetTick();
    while (GetTick() - start_time < timeout_ms)
    {
        // 检查底层是否收到了数据 (可能是完整包，也可能是碎片)
        if (UART3_RxFlag)
        {
            // A. 将底层收到的数据追加到我们的累加缓冲区中
            if (acc_len + UART3_RxLength < sizeof(rx_accumulator) - 1)
            {
                memcpy(rx_accumulator + acc_len, UART3_RxBuffer, UART3_RxLength);
                acc_len += UART3_RxLength;
                rx_accumulator[acc_len] = '\0'; // 确保字符串结束符
            }
            
            // B. [关键] 立即重置底层DMA，以便接收剩余的数据碎片
            // 如果不重置，DMA处于关闭状态，后续数据会丢失
            BSP_UART3_ResetRx();
            
            // C. 检查累加缓冲区中是否包含期望的字符串
            if (strstr(rx_accumulator, expect) != NULL)
            {
                return true; // 成功找到
            }
        }
    }
    
    // 超时，打印累加缓冲区的内容以便调试
    printf("[LoRa Error] Cmd: %s Timeout! Rx: %s\r\n", cmd, rx_accumulator);
    return false;
}

/**
 * @brief  LoRa 初始化并执行自检流程
 */
bool LoRa_Init_And_SelfCheck(uint16_t addr)
{
    char cmd_buf[64];
    char expect_buf[32];
    
    printf("\r\n[LoRa] === Start Self-Check & Config ===\r\n");
    
    // 1. 进入配置模式
    LoRa_SetMode_Config();
    BSP_UART3_Init(115200); 
    Delay_ms(100);
    
    // 2. 握手测试 (Ping)
    bool ping_ok = false;
    for(int i=0; i<3; i++) {
        if(LoRa_SendCmd_WaitResp("AT\r\n", "OK", 200)) {
            ping_ok = true;
            printf("[LoRa] Ping: OK\r\n");
            break;
        }
        printf("[LoRa] Ping: Retry %d...\r\n", i+1);
        Delay_ms(100);
    }
    if(!ping_ok) return false; 

    // 3. 关闭回显 (ATE0)
    if(!LoRa_SendCmd_WaitResp("ATE0\r\n", "OK", 500)) return false;
    printf("[LoRa] Echo Off: OK\r\n");
    
    // 4. [修改] 移除 AT+DEFAULT，避免重置导致后续参数写入失败
    // if(!LoRa_SendCmd_WaitResp("AT+DEFAULT\r\n", "OK", 2000)) return false;
    // printf("[LoRa] Factory Reset: OK\r\n");
    
    // 5. [新增] 显式开启参数保存 (AT+FLASH=1)
    // 确保配置掉电不丢失，且立即生效
    if(!LoRa_SendCmd_WaitResp("AT+FLASH=1\r\n", "OK", 500)) return false;

    // 6. 配置地址
    sprintf(cmd_buf, "AT+ADDR=%02X,%02X\r\n", (addr >> 8) & 0xFF, addr & 0xFF);
    if(!LoRa_SendCmd_WaitResp(cmd_buf, "OK", 500)) return false;
    
    // 7. 配置信道和空速 (信道30, 空速2.4k - 提高抗干扰)
    // 注意：这里我们统一改为信道30，空速2 (2.4kbps)
    if(!LoRa_SendCmd_WaitResp("AT+WLRATE=30,2\r\n", "OK", 500)) return false;
    
    // 8. 配置透传模式
    if(!LoRa_SendCmd_WaitResp("AT+TMODE=0\r\n", "OK", 500)) return false;
    
    // 9. 配置一般工作模式
    if(!LoRa_SendCmd_WaitResp("AT+CWMODE=0\r\n", "OK", 500)) return false;
    
    // [新增] 配置发射功率为最低 (0 = 11dBm)
    // 默认是 3 (20dBm)，近距离测试必须调低
    if(!LoRa_SendCmd_WaitResp("AT+TPOWER=0\r\n", "OK", 500)) return false;		
		
		
    // 10. 配置通信波特率 (115200)
    if(!LoRa_SendCmd_WaitResp("AT+UART=7,0\r\n", "OK", 500)) return false;
    printf("[LoRa] Parameters Set: OK\r\n");
    
    // 尝试1：大写匹配
    sprintf(expect_buf, "+ADDR:%02X,%02X", (addr >> 8) & 0xFF, addr & 0xFF);
    if(!LoRa_SendCmd_WaitResp("AT+ADDR?\r\n", expect_buf, 500)) {
        
        // 尝试2：小写匹配 (针对 ATK-LORA-01 的某些固件)
        sprintf(expect_buf, "+ADDR:%02x,%02x", (addr >> 8) & 0xFF, addr & 0xFF);
        if(!LoRa_SendCmd_WaitResp("AT+ADDR?\r\n", expect_buf, 500)) {
            printf("[LoRa] Verify Addr Failed! Expected: %s (Case Insensitive)\r\n", expect_buf);
            return false;
        }
    }
    
    // 验证波特率
    if(!LoRa_SendCmd_WaitResp("AT+UART?\r\n", "+UART:7,0", 500)) {
        printf("[LoRa] Verify UART Failed!\r\n");
        return false;
    }
    printf("[LoRa] Verification: OK\r\n");
    
    // 12. 切换回通信模式
    LoRa_SetMode_Trans();
    BSP_UART3_Init(115200); 
    BSP_UART3_ClearRxBuffer();
    
    printf("[LoRa] === Ready (Trans Mode 115200) ===\r\n\r\n");
    return true;
}
