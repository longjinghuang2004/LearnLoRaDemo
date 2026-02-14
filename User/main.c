/**
  ******************************************************************************
  * @file    main.c
  * @author  LoRaPlat Team
  * @brief   LoRaPlat V3.6 Echo 测试 (适配停等协议与异步回复)
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

volatile uint8_t g_TimeoutFlag;

extern void Demo_OSAL_Init(void); 

// ============================================================================
// [测试角色配置]
// 1 = HOST (主机): 定时发送 PING
// 2 = SLAVE (从机): 收到数据后回显 Echo
// ============================================================================
#define TEST_ROLE      2

// ============================================================================
// 全局变量 (用于异步处理)
// ============================================================================
#if (TEST_ROLE == 2)
    // 从机回响缓冲区
    static char g_EchoBuffer[64];
    static uint16_t g_EchoTargetID = 0;
    static volatile bool g_NeedEcho = false;
#endif

// ============================================================================
// 1. 接口适配 (Adapter Layer)
// ============================================================================

void Adapter_SaveConfig(const LoRa_Config_t *cfg) {
    Flash_WriteLoRaConfig(cfg);
    Serial_Printf("[APP] Config Saved.\r\n");
}

void Adapter_LoadConfig(LoRa_Config_t *cfg) {
    Flash_ReadLoRaConfig(cfg);
}

uint32_t Adapter_GetRandomSeed(void) {
    return LoRa_Port_GetEntropy32(); 
}

void Adapter_SystemReset(void) {
    Serial_Printf("[APP] Resetting...\r\n");
    for(volatile int i=0; i<1000000; i++); 
    NVIC_SystemReset();
}

// 接收数据回调
void Adapter_OnRecvData(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta) {
    // 打印接收到的数据
    // 注意：data 末尾不一定有 \0，打印字符串需小心，这里假设发送方都带了 \0 或手动限制长度
    Serial_Printf("[APP] RX from 0x%04X: %.*s\r\n", src_id, len, data);
    
    // LED 指示
    LED2_Turn();

#if (TEST_ROLE == 2)
    // [从机逻辑] 收到非广播包时，准备 Echo
    // 注意：不能在这里直接 Send，因为此时 FSM 正在 ACK_DELAY 状态 (Busy)
    if (src_id != 0xFFFF && !g_NeedEcho) {
        // 简单的防重入保护，如果上一条还没回，这就丢弃（或者你可以做一个应用层队列）
        int copy_len = (len > 50) ? 50 : len; // 限制长度防止溢出
        snprintf(g_EchoBuffer, sizeof(g_EchoBuffer), "Echo: %.*s", copy_len, data);
        g_EchoTargetID = src_id;
        g_NeedEcho = true; // 标记需要发送，交给 main 循环处理
    }
#endif
}

void Adapter_OnEvent(LoRa_Event_t event, void *arg) {
    switch(event) {
        case LORA_EVENT_INIT_SUCCESS:
            Serial_Printf("[EVT] Init OK.\r\n");
            break;
        case LORA_EVENT_MSG_SENT:
            Serial_Printf("[EVT] TX Done.\r\n");
            break;
        default: break;
    }
}

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
    Flash_ReadLoRaConfig(&cfg);
    
    uint16_t target_id = (TEST_ROLE == 1) ? 1 : 2;
    
    if (cfg.net_id != target_id || cfg.magic != LORA_CFG_MAGIC) {
        Serial_Printf("[TEST] Forcing Config ID=%d...\r\n", target_id);
        memset(&cfg, 0, sizeof(LoRa_Config_t));
        cfg.magic = LORA_CFG_MAGIC;
        cfg.net_id = target_id;
        cfg.group_id = 100; 
        cfg.uuid = (TEST_ROLE == 1) ? 0xAAAA1111 : 0xBBBB2222;
        cfg.hw_addr = LORA_HW_ADDR_DEFAULT;
        cfg.channel = DEFAULT_LORA_CHANNEL;
        cfg.power = DEFAULT_LORA_POWER;
        cfg.air_rate = DEFAULT_LORA_RATE;
        cfg.tmode = DEFAULT_LORA_TMODE;
        Flash_WriteLoRaConfig(&cfg);
        Adapter_SystemReset();
    }
}

// ============================================================================
// 3. 主函数
// ============================================================================

int main(void)
{
    SysTick_Init();
    LED_Init();
    Serial_Init();
    Demo_OSAL_Init();
    
    // 强制配置 ID
    Force_Init_Config();
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    
    Serial_Printf("\r\n=== LoRaPlat Echo Test (Role: %s) ===\r\n", (TEST_ROLE==1)?"HOST":"SLAVE");

    LoRa_Service_Init(&my_adapter, 0); 

    uint32_t last_blink = 0;
    
#if (TEST_ROLE == 1)
    uint32_t last_ping = 0;
    uint32_t ping_cnt = 0;
    char input_buf[128];
#endif

    while (1)
    {
        // 1. 协议栈驱动
        LoRa_Service_Run();

        // 2. 业务逻辑
#if (TEST_ROLE == 1)
        // --- 主机逻辑 ---
        
        // A. 串口指令控制
        if (Serial_GetRxPacket(input_buf, sizeof(input_buf))) {
            // ... (保留之前的 CMD 解析逻辑，此处省略以精简代码) ...
            // 你可以保留之前的 CMD 解析代码用于手动测试
             Serial_Printf("[PC] Input: %s\r\n", input_buf);
             // 简单透传测试
             LoRa_Service_Send((uint8_t*)input_buf, strlen(input_buf), 2);
        }
        
        // B. 自动 PING 测试 (每 3 秒一次)
        // 只有当系统不忙时才发，避免阻塞
        if (GetTick() - last_ping > 3000) {
            // 检查底层是否忙 (例如正在重传上一包)
            // 注意：LoRa_Manager_IsBusy() 只能检查 FSM 状态，不能检查 RingBuffer 是否满
            // 但 Send 函数内部会检查 FSM 状态并返回 false
            
            char ping_msg[32];
            sprintf(ping_msg, "PING-%d", ++ping_cnt);
            
            Serial_Printf(" -> Auto Sending: %s\r\n", ping_msg);
            
            if (LoRa_Service_Send((uint8_t*)ping_msg, strlen(ping_msg), 2)) {
                last_ping = GetTick();
            } else {
                Serial_Printf(" -> Skip: System Busy\r\n");
                // 下次循环重试
            }
        }
#endif

#if (TEST_ROLE == 2)
        // --- 从机逻辑 ---
        
        // 异步 Echo 处理
        if (g_NeedEcho) {
            // 尝试发送 Echo
            // Send 内部会检查 FSM 状态。
            // 如果 FSM 还在发 ACK (ACK_DELAY 或 WAIT_ACK)，Send 会返回 false。
            // 我们就等到它返回 true 为止。
            
            if (LoRa_Service_Send((uint8_t*)g_EchoBuffer, strlen(g_EchoBuffer), g_EchoTargetID)) {
                Serial_Printf(" -> Echo Sent: %s\r\n", g_EchoBuffer);
                g_NeedEcho = false; // 清除标志
            }
            // else: System Busy, wait for next loop
        }
#endif

        // 3. LED 心跳
        if (GetTick() - last_blink > 100) { 
            last_blink = GetTick();
            LED1_Turn(); 
        }
    }
}
