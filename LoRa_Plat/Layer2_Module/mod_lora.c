#include "mod_lora.h"
#include "bsp_uart3_dma.h"
#include "Delay.h"
#include "Serial.h"
#include <stdio.h>

// ... 引脚定义和 GPIO_Init, WaitAux 保持不变 ...

// ... LoRa_Init, SetMode_Config, SetMode_Trans 保持不变 ...

/**
 * @brief  自动配置 LoRa 模块
 * @param  addr: 本机地址
 */
void LoRa_AutoConfig(uint16_t addr)
{
    char cmd_buf[64];

    printf("[LoRa] Start Auto Config for Address: 0x%04X\r\n", addr);

    // 1. 进入配置模式
    LoRa_SetMode_Config();

    // 2. [修正] 配置模式下，ATK-LORA-01 固定波特率为 115200
    // 手册说明：模块在AT指令操作下，固定串口波特率为115200
    BSP_UART3_Init(115200); 
    Delay_ms(100);

    // 3. 恢复出厂设置
    // 注意：恢复出厂后，通信波特率变回 9600，但配置模式仍是 115200
    BSP_UART3_SendString("AT+DEFAULT\r\n");
    Delay_ms(1000); 

    // 4. 设置地址
    sprintf(cmd_buf, "AT+ADDR=%02X,%02X\r\n", (addr >> 8) & 0xFF, addr & 0xFF);
    BSP_UART3_SendString(cmd_buf);
    Delay_ms(200);

    // 5. 设置信道和空速 (信道23, 空速19.2k)
    BSP_UART3_SendString("AT+WLRATE=23,5\r\n");
    Delay_ms(200);

    // 6. 设置发送模式 (透传)
    BSP_UART3_SendString("AT+TMODE=0\r\n");
    Delay_ms(200);

    // 7. 设置工作模式 (一般模式)
    BSP_UART3_SendString("AT+CWMODE=0\r\n");
    Delay_ms(200);

    // 8. [关键] 设置通信波特率为 115200
    // 这一步告诉模块：当你退出配置模式后，请用 115200 和我通信
    BSP_UART3_SendString("AT+UART=7,0\r\n");
    Delay_ms(500); 

    printf("[LoRa] Config Commands Sent.\r\n");

    // 9. 切换回通信模式 (透传)
    LoRa_SetMode_Trans();

    // 10. STM32 保持 115200 (因为我们刚才设置模块也是 115200)
    // 重新初始化一下以清空之前的缓冲区垃圾
    BSP_UART3_Init(115200);
    
    // [新增] 强制清空接收标志，防止模式切换产生的乱码触发接收逻辑
    BSP_UART3_ResetRx(); 
    
    printf("[LoRa] Switched to Trans Mode (115200 bps). Ready.\r\n");
}
