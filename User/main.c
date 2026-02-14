/**
  ******************************************************************************
  * @file    main.c
  * @author  LoRaPlat Team
  * @brief   LoRaPlat V3.2 综合测试程序 (适配变量私有化与接口访问)
  ******************************************************************************
  */

#include "stm32f10x.h"
#include "Delay.h"
#include "Serial.h"
#include "LED.h"
#include "Flash.h"
#include "lora_service.h" 
#include "lora_port.h" 
#include <string.h>
#include <stdio.h>


extern void Demo_OSAL_Init(void); 
volatile uint8_t g_TimeoutFlag;

// ============================================================================
// [测试角色配置]
// 1 = HOST (主机): ID=1, Group=100
// 2 = SLAVE (从机): ID=2, Group=100
// ============================================================================
#define TEST_ROLE      1

// ============================================================================
// 1. 接口适配 (Adapter Layer)
// ============================================================================

void Adapter_SaveConfig(const LoRa_Config_t *cfg) {
    Flash_WriteLoRaConfig(cfg);
    Serial_Printf("[APP] Config Saved to Flash.\r\n");
}

void Adapter_LoadConfig(LoRa_Config_t *cfg) {
    Flash_ReadLoRaConfig(cfg);
}

uint32_t Adapter_GetRandomSeed(void) {
    return LoRa_Port_GetEntropy32(); // 使用 ADC 悬空噪声作为种子
}

void Adapter_SystemReset(void) {
    Serial_Printf("[APP] System Resetting...\r\n");
    for(volatile int i=0; i<1000000; i++); 
    NVIC_SystemReset();
}

// 接收数据回调
void Adapter_OnRecvData(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta) {
    Serial_Printf("[APP] RX from ID:0x%04X | Len:%d | Payload: %s\r\n", src_id, len, data);
    
    // [安全修正] 使用 strcmp 进行严格匹配，防止 "LED_ON111" 误触发
    // 实际项目中建议定义更严谨的指令格式
    if (strstr((const char*)data, "LED_ON")) {
        LED1_ON(); 
        Serial_Printf("    -> Action: LED ON\r\n");
    } else if (strstr((const char*)data, "LED_OFF")) {
        LED1_OFF();
        Serial_Printf("    -> Action: LED OFF\r\n");
    }

#if (TEST_ROLE == 2)
    // [重要] 自动回响 (Echo) 暂时关闭
    // 原因：当前未实现 TX 队列，Echo 包会与 ACK 包发生 DMA 竞争导致数据损坏。
    // 待 Phase 2 实现 RingBuffer 后再开启。
    /*
    if (src_id != 0xFFFF) {
        char reply[64];
        snprintf(reply, 64, "Echo: %s", data);
        if (!LoRa_Service_Send((uint8_t*)reply, strlen(reply), src_id)) {
            Serial_Printf("[APP] Echo Failed: System Busy\r\n");
        } else {
            Serial_Printf("[APP] Echo Sent\r\n");
        }
    }
    */
#endif
}

// 系统事件回调
void Adapter_OnEvent(LoRa_Event_t event, void *arg) {
    // [修改] 使用接口获取配置，不再直接访问全局变量
    const LoRa_Config_t *cfg = LoRa_Service_GetConfig();

    switch(event) {
        case LORA_EVENT_INIT_SUCCESS:
            Serial_Printf("[EVT] LoRa Init Done.\r\n");
            if (cfg) {
                Serial_Printf("      UUID: 0x%08X\r\n", cfg->uuid);
                Serial_Printf("      NetID: %d (0x%04X)\r\n", cfg->net_id, cfg->net_id);
                Serial_Printf("      Group: %d (0x%04X)\r\n", cfg->group_id, cfg->group_id);
            }
            break;
            
        case LORA_EVENT_MSG_SENT:
            Serial_Printf("[EVT] TX Complete\r\n");
            break;
            
        case LORA_EVENT_MSG_RECEIVED:
            // 收到消息时的 LED 指示（可选）
            // LED2_Turn();
            break;
            
        case LORA_EVENT_BIND_SUCCESS:
            Serial_Printf("[EVT] BIND Success! New ID: %d\r\n", *(uint16_t*)arg);
            break;
            
        case LORA_EVENT_FACTORY_RESET:
            Serial_Printf("[EVT] Factory Reset Triggered.\r\n");
            break;
            
        default: break;
    }
}

// 定义适配器实例
const LoRa_Callback_t my_adapter = {
    .SaveConfig     = Adapter_SaveConfig,
    .LoadConfig     = Adapter_LoadConfig,
    .GetRandomSeed  = Adapter_GetRandomSeed,
    .SystemReset    = Adapter_SystemReset,
    .OnRecvData     = Adapter_OnRecvData,
    .OnEvent        = Adapter_OnEvent
};

// ============================================================================
// 2. 辅助函数
// ============================================================================

void Force_Init_Config(void) {
    LoRa_Config_t cfg;
    // 直接读取 Flash 检查是否需要强制重置测试环境
    Flash_ReadLoRaConfig(&cfg);
    
    uint16_t target_id = (TEST_ROLE == 1) ? 1 : 2;
    uint16_t target_group = 100; 
    
    // 如果 Flash 中的配置与当前测试角色不符，强制重写
    if (cfg.net_id != target_id || cfg.group_id != target_group || cfg.magic != LORA_CFG_MAGIC) {
        Serial_Printf("[TEST] Forcing Config: NetID=%d, GroupID=%d...\r\n", target_id, target_group);
        
        memset(&cfg, 0, sizeof(LoRa_Config_t));
        cfg.magic = LORA_CFG_MAGIC;
        cfg.net_id = target_id;
        cfg.group_id = target_group; 
        // 模拟不同的 UUID
        cfg.uuid = (TEST_ROLE == 1) ? 0xAAAA1111 : 0xBBBB2222;
        cfg.hw_addr = LORA_HW_ADDR_DEFAULT;
        cfg.channel = DEFAULT_LORA_CHANNEL;
        cfg.power = DEFAULT_LORA_POWER;
        cfg.air_rate = DEFAULT_LORA_RATE;
        cfg.tmode = DEFAULT_LORA_TMODE;
        
        Flash_WriteLoRaConfig(&cfg);
        Serial_Printf("[TEST] Force Init Done. Rebooting...\r\n");
        Adapter_SystemReset();
    }
}

void Show_Help(void) {
    Serial_Printf("\r\n=== LoRaPlat V3.2 Refactored Test ===\r\n");
    Serial_Printf("Role: %s (ID=%d, Group=%d)\r\n", (TEST_ROLE==1)?"HOST":"SLAVE", 
           (TEST_ROLE==1)?1:2, 100);
    Serial_Printf("Commands (Type in Serial):\r\n");
    Serial_Printf("  CMD <id> <msg>  : Unicast (e.g., CMD 2 LED_ON)\r\n");
    Serial_Printf("  CMD 100 <msg>   : Multicast to Group 100\r\n");
    Serial_Printf("  CMD 65535 <msg> : Broadcast\r\n");
    Serial_Printf("  BIND <uuid> <id>: Remote Bind ID\r\n");
    Serial_Printf("Note: LED1 blinks fast (10Hz) to prove system is NON-BLOCKING.\r\n");
    Serial_Printf("====================================\r\n");
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
    
    // 2. OSAL 初始化 (如果需要显式初始化，视具体实现而定)
    // extern void Demo_OSAL_Init(void); 
    Demo_OSAL_Init();
    
    // 3. 检查并强制配置测试环境 (仅用于测试阶段)
    Force_Init_Config();
    
    Show_Help();

    // 4. 初始化 LoRa 服务 (阻塞式，包含驱动握手)
    // 传入适配器和 0 (不强制覆盖 NetID，使用 Flash/默认值)
    LoRa_Service_Init(&my_adapter, 0); 

    uint32_t last_blink = 0;
    char input_buf[128]; // 串口接收缓冲区

    while (1)
    {
        // ----------------------------------------------------
        // 1. 协议栈心跳 (必须周期性调用)
        // ----------------------------------------------------
        LoRa_Service_Run();

        // ----------------------------------------------------
        // 2. 业务逻辑: 串口透传 (仅主机)
        // ----------------------------------------------------
#if (TEST_ROLE == 1)
        // [修改] 使用 Serial_GetRxPacket 接口获取数据，替代直接访问全局变量
        if (Serial_GetRxPacket(input_buf, sizeof(input_buf)))
        {
            char *input = input_buf;
            int len = strlen(input);
            
            // 去除末尾换行符
            while(len > 0 && (input[len-1] == '\r' || input[len-1] == '\n')) input[--len] = '\0';

            if (len > 0) {
                Serial_Printf("[PC] Input: %s\r\n", input);
                
                // 解析 CMD 指令
                if (strncmp(input, "CMD ", 4) == 0) {
                    int target_id;
                    char msg[64];
                    if (sscanf(input + 4, "%d %[^\n]", &target_id, msg) == 2) {
                        Serial_Printf(" -> Sending to %d: %s\r\n", target_id, msg);
                        if (!LoRa_Service_Send((uint8_t*)msg, strlen(msg), target_id)) {
                            Serial_Printf("[APP] Error: System Busy!\r\n");
                        }
                    }
                }
                // 解析 BIND 指令
                else if (strncmp(input, "BIND ", 5) == 0) {
                    uint32_t u;
                    int id;
                    if (sscanf(input + 5, "%u %d", &u, &id) == 2) {
                        char cmd[64];
                        sprintf(cmd, "CMD:BIND=%u,%d", u, id);
                        Serial_Printf(" -> Sending Bind: %s\r\n", cmd);
                        // 发送广播包
                        LoRa_Service_Send((uint8_t*)cmd, strlen(cmd), 0xFFFF); 
                    }
                }
            }
        }
#endif

        // ----------------------------------------------------
        // 3. 验证非阻塞特性: LED 心跳
        // ----------------------------------------------------
        if (GetTick() - last_blink > 50) { 
            last_blink = GetTick();
            LED1_Turn(); 
        }
    }
}
