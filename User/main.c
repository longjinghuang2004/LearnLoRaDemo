/**
  ******************************************************************************
  * @file    main.c
  * @author  LoRaPlat Team
  * @brief   LoRaPlat V3.0 综合测试程序
  * @note    本程序演示了如何集成 LoRaPlat V3.0 协议栈，并实现以下功能：
  *          1. 依赖注入 (Adapter Pattern)
  *          2. 非阻塞主循环 (Async Loop)
  *          3. P2P/广播/群播通信
  *          4. 远程 LED 控制与 Echo 回响
  ******************************************************************************
  */


#include "stm32f10x.h"
#include "Delay.h"
#include "Serial.h"
#include "LED.h"
#include "Flash.h"
#include "lora_service.h" 
#include "lora_port.h" // 仅用于 Port_Init_STM32
#include <string.h>
#include <stdio.h>

volatile uint8_t g_TimeoutFlag;

// ============================================================================
// [测试角色配置] - 编译前请修改此处
// ============================================================================
// 1 = HOST (主机): ID=1, Group=100, 发送指令控制从机
// 2 = SLAVE (从机): ID=2, Group=100, 接收指令并回响(Echo)
// ============================================================================
#define TEST_ROLE      2

// ============================================================================
// 1. 接口适配 (Adapter Layer)
// ============================================================================

// Flash 保存适配
void Adapter_SaveConfig(const LoRa_Config_t *cfg) {
    Flash_WriteLoRaConfig(cfg);
    printf("[APP] Config Saved to Flash.\r\n");
}

// Flash 读取适配
void Adapter_LoadConfig(LoRa_Config_t *cfg) {
    Flash_ReadLoRaConfig(cfg);
}

// 系统时基适配
uint32_t Adapter_GetTick(void) {
    return GetTick();
}

// 随机数适配 (利用悬空ADC引脚)
uint32_t Adapter_GetRandomSeed(void) {
    // 简单实现：读取 SysTick 和 未初始化内存的异或值
    return GetTick() ^ (*(uint32_t*)0x20000000); 
}

// 系统复位适配
void Adapter_SystemReset(void) {
    printf("[APP] System Resetting...\r\n");
    // 允许短暂阻塞以确保打印完成
    for(volatile int i=0; i<1000000; i++); 
    NVIC_SystemReset();
}

// [关键] 接收数据回调
void Adapter_OnRecvData(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta) {
    // 1. 打印接收到的数据
    printf("[APP] RX from ID:0x%04X | Len:%d | RSSI:%d | Payload: %s\r\n", 
           src_id, len, meta->rssi, data);
    
    // 2. 业务逻辑: LED 控制
    if (strstr((const char*)data, "LED_ON")) {
        LED1_ON(); 
        printf("    -> Action: LED ON\r\n");
    } else if (strstr((const char*)data, "LED_OFF")) {
        LED1_OFF();
        printf("    -> Action: LED OFF\r\n");
    }

    // 3. [从机模式] 自动回响 (Echo)
#if (TEST_ROLE == 2)
    // 防止自己回响自己的广播包 (虽然协议栈已过滤，但加一层保险)
    if (src_id != 0xFFFF) {
        char reply[64];
        snprintf(reply, 64, "Echo: %s", data);
        
        // 尝试发送回响 (单播给源地址)
        if (!Service_Send((uint8_t*)reply, strlen(reply), src_id)) {
            printf("[APP] Echo Failed: System Busy\r\n");
        } else {
            printf("[APP] Echo Sent\r\n");
        }
    }
#endif
}

// [关键] 系统事件回调
void Adapter_OnEvent(LoRa_Event_t event, void *arg) {
    // 1. 【修复】在这里获取配置指针，定义 cfg 变量
    const LoRa_Config_t *cfg = Service_GetConfig();

    switch(event) {
        case LORA_EVT_INIT_DONE:
            printf("[EVT] LoRa Init Done.\r\n");
            // 2. 现在可以使用 cfg 指针了
            if (cfg) {
                printf("      UUID: 0x%08X\r\n", cfg->uuid);
                printf("      NetID: %d (0x%04X)\r\n", cfg->net_id, cfg->net_id);
                printf("      Group: %d (0x%04X)\r\n", cfg->group_id, cfg->group_id);
            }
            break;
            
        case LORA_EVT_MSG_SENT:
            printf("[EVT] TX Complete (Async Callback)\r\n");
            break;
            
        case LORA_EVT_MSG_RECV:
            // 物理层收到包的瞬间，LED2 闪一下指示
            LED2_Turn();
            break;
            
        case LORA_EVT_BIND_SUCCESS:
            printf("[EVT] BIND Success! New ID: %d\r\n", *(uint16_t*)arg);
            break;
            
        case LORA_EVT_FACTORY_RESET:
            printf("[EVT] Factory Reset Triggered.\r\n");
            break;
            
        default: break;
    }
}

            


// 定义适配器实例
const LoRa_App_Adapter_t my_adapter = {
    .SaveConfig     = Adapter_SaveConfig,
    .LoadConfig     = Adapter_LoadConfig,
    .GetTick        = Adapter_GetTick,
    .GetRandomSeed  = Adapter_GetRandomSeed,
    .SystemReset    = Adapter_SystemReset,
    .OnRecvData     = Adapter_OnRecvData,
    .OnEvent        = Adapter_OnEvent
};

// ============================================================================
// 2. 辅助函数
// ============================================================================

// 强制初始化配置 (仅用于测试环境，确保 ID 正确)
void Force_Init_Config(void) {
    LoRa_Config_t cfg;
    Flash_ReadLoRaConfig(&cfg);
    
    uint16_t target_id = (TEST_ROLE == 1) ? 1 : 2;
    uint16_t target_group = 100; 
    
    // 如果 Flash 中的配置与当前测试角色不符，或者 Magic 不对，则强制重写
    if (cfg.net_id != target_id || cfg.group_id != target_group || cfg.magic != LORA_CFG_MAGIC) {
        printf("[TEST] Forcing Config: NetID=%d, GroupID=%d...\r\n", target_id, target_group);
        
        memset(&cfg, 0, sizeof(LoRa_Config_t));
        cfg.magic = LORA_CFG_MAGIC;
        cfg.net_id = target_id;
        cfg.group_id = target_group; 
        cfg.uuid = (TEST_ROLE == 1) ? 0xAAAA1111 : 0xBBBB2222;
        cfg.hw_addr = LORA_HW_ADDR_DEFAULT;
        cfg.channel = DEFAULT_LORA_CHANNEL;
        cfg.power = DEFAULT_LORA_POWER;
        cfg.air_rate = DEFAULT_LORA_RATE;
        cfg.tmode = DEFAULT_LORA_TMODE;
        
        Flash_WriteLoRaConfig(&cfg);
        printf("[TEST] Force Init Done. Rebooting...\r\n");
        Adapter_SystemReset();
    }
}

void Show_Help(void) {
    printf("\r\n=== LoRaPlat V3.0 Async FSM Test ===\r\n");
    printf("Role: %s (ID=%d, Group=%d)\r\n", (TEST_ROLE==1)?"HOST":"SLAVE", 
           (TEST_ROLE==1)?1:2, 100);
    printf("Commands (Type in Serial):\r\n");
    printf("  CMD <id> <msg>  : Unicast (e.g., CMD 2 LED_ON)\r\n");
    printf("  CMD 100 <msg>   : Multicast to Group 100\r\n");
    printf("  CMD 65535 <msg> : Broadcast\r\n");
    printf("  BIND <uuid> <id>: Remote Bind ID\r\n");
    printf("Note: LED1 blinks fast (10Hz) to prove system is NON-BLOCKING.\r\n");
    printf("====================================\r\n");
}

// ============================================================================
// 3. 主函数
// ============================================================================

int main(void)
{
    // 1. 基础硬件初始化
    SysTick_Init();
    LED_Init();
    Serial_Init();
    
    // 2. 显式初始化 Port 层 (绑定 STM32 硬件)
    Port_Init_STM32();
    
    // 3. 强制配置 (仅测试用)
    Force_Init_Config();
    
    // 4. 打印欢迎信息
    Show_Help();

    // 5. LoRa 服务初始化 (依赖注入)
    // 内部会启动 FSM 进行复位，此时 LoRa 模块可能还在拉低 RST，但这里已经返回了
    Service_Init(&my_adapter, 0); 

    uint32_t last_blink = 0;

    while (1)
    {
        // ---------------------------------------------------------
        // 1. 协议栈心跳 (必须高频调用)
        // ---------------------------------------------------------
        // 所有的超时检测、状态跳转、数据接收都在这里发生
        Service_Run();

        // ---------------------------------------------------------
        // 2. 业务逻辑: 串口透传 (仅主机)
        // ---------------------------------------------------------
#if (TEST_ROLE == 1)
        if (Serial_RxFlag == 1)
        {
            char *input = Serial_RxPacket;
            int len = strlen(input);
            // 去除换行符
            while(len > 0 && (input[len-1] == '\r' || input[len-1] == '\n')) input[--len] = '\0';

            if (len > 0) {
                printf("[PC] Input: %s\r\n", input);
                
                // 解析指令: CMD <target_id> <msg>
                if (strncmp(input, "CMD ", 4) == 0) {
                    int target_id;
                    char msg[64];
                    if (sscanf(input + 4, "%d %[^\n]", &target_id, msg) == 2) {
                        printf(" -> Sending to %d: %s\r\n", target_id, msg);
                        if (!Service_Send((uint8_t*)msg, strlen(msg), target_id)) {
                            printf("[APP] Error: System Busy!\r\n");
                        }
                    }
                }
                // 解析绑定: BIND <uuid> <id>
                else if (strncmp(input, "BIND ", 5) == 0) {
                    uint32_t u;
                    int id;
                    if (sscanf(input + 5, "%u %d", &u, &id) == 2) {
                        char cmd[64];
                        sprintf(cmd, "CMD:BIND=%u,%d", u, id);
                        printf(" -> Sending Bind: %s\r\n", cmd);
                        Service_Send((uint8_t*)cmd, strlen(cmd), 0xFFFF); // 广播绑定
                    }
                }
                else {
                    printf("[APP] Unknown Command. Try 'CMD 2 LED_ON'\r\n");
                }
            }
            Serial_RxFlag = 0; 
        }
#endif

        // ---------------------------------------------------------
        // 3. 验证非阻塞特性: LED 心跳
        // ---------------------------------------------------------
        // 无论 LoRa 是否在发送、复位或等待超时，这个 LED 都应该均匀闪烁
        if (GetTick() - last_blink > 50) { // 10Hz 极速闪烁
            last_blink = GetTick();
            LED1_Turn(); 
        }
    }
}
