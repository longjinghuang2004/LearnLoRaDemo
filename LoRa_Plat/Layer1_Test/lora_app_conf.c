/**
  ******************************************************************************
  * @file    lora_app_conf.c
  * @brief   Layer 1: LoRa 应用层配置 (适配层)
  *          负责实例化 LoRa 对象并绑定底层驱动
  ******************************************************************************
  */

#include "lora_app_conf.h"
#include "bsp_lora_uart.h" // Layer 3
#include "Delay.h"         // System
#include "stm32f10x.h"

// --- 全局 LoRa 设备对象 ---
LoRa_Dev_t g_LoRa;

// --- 适配函数 (Wrapper Functions) ---
// 将 Layer 3 的函数签名转换为 Layer 2 需要的签名

static uint16_t Port_Send(const uint8_t *data, uint16_t len)
{
    return BSP_LoRa_UART_Send(data, len);
}

static uint16_t Port_Read(uint8_t *buf, uint16_t max_len)
{
    return BSP_LoRa_UART_Read(buf, max_len);
}

static void Port_SetMD0(uint8_t level)
{
    // MD0 -> PA4
    GPIO_WriteBit(GPIOA, GPIO_Pin_4, (BitAction)level);
}

static uint8_t Port_ReadAUX(void)
{
    // AUX -> PA5
    return GPIO_ReadInputDataBit(GPIOA, GPIO_Pin_5);
}

static void Port_DelayMs(uint32_t ms)
{
    Delay_ms(ms);
}

static uint32_t Port_GetTick(void)
{
    return GetTick();
}

// --- 初始化函数 ---
void LoRa_App_Init(void)
{
    // 1. 初始化底层硬件 (Layer 3)
    BSP_LoRa_UART_Init();
    
    // 2. 初始化 GPIO (MD0, AUX) - 这一步其实在 BSP_LoRa_UART_Init 里没做，需补充
    //    注意：为了解耦，BSP_LoRa_UART_Init 只负责 UART。
    //    MD0/AUX 属于控制引脚，可以在这里初始化，或者在 BSP 里加。
    //    为了方便，我们在这里补充初始化 PA4, PA5。
    RCC_APB2PeriphClockCmd(RCC_APB2Periph_GPIOA, ENABLE);
    GPIO_InitTypeDef GPIO_InitStructure;
    
    // PA4 -> MD0 (推挽输出)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_4;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_Out_PP;
    GPIO_InitStructure.GPIO_Speed = GPIO_Speed_50MHz;
    GPIO_Init(GPIOA, &GPIO_InitStructure);
    
    // PA5 -> AUX (上拉输入)
    GPIO_InitStructure.GPIO_Pin = GPIO_Pin_5;
    GPIO_InitStructure.GPIO_Mode = GPIO_Mode_IPU;
    GPIO_Init(GPIOA, &GPIO_InitStructure);

    // 3. 填充驱动接口结构体
    LoRa_Driver_t driver = {
        .Send = Port_Send,
        .Read = Port_Read,
        .SetMD0 = Port_SetMD0,
        .ReadAUX = Port_ReadAUX,
        .DelayMs = Port_DelayMs,
        .GetTick = Port_GetTick
    };

    // 4. 初始化 LoRa 中间件 (Layer 2)
    if (LoRa_Init(&g_LoRa, &driver))
    {
        // 5. 配置包头包尾 (根据需求: CM.../n/n)
        //    注意：C语言中 '\n' 是一个字符 (0x0A)。
        //    如果你指的是字符串 "/n"，那就是 '/' 和 'n'。
        //    通常协议里指的是换行符，这里我们按之前的约定配置为 CM 和 \n\n
        LoRa_SetPacketHeader(&g_LoRa, 'C', 'M');
        LoRa_SetPacketTail(&g_LoRa, '\n', '\n');
    }
}
