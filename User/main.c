#include "stm32f10x.h"
#include "Delay.h"
#include "Serial.h"
#include "LED.h"
#include "Flash.h"
#include "lora_service.h" 
#include <string.h>
#include <stdio.h>

volatile uint8_t g_TimeoutFlag;

// ============================================================================
// [测试角色配置] - 编译前请修改此处
// ============================================================================
// 1 = HOST (主机): 连接PC串口助手，发送指令，控制从机
// 2 = SLAVE (从机): 独立运行，接收指令并回响(Echo)，执行动作
// ============================================================================
#define TEST_ROLE      2

// ============================================================================
// 1. 接口适配 (Adapter Layer)
// ============================================================================

void Adapter_SaveConfig(const LoRa_Config_t *cfg) {
    Flash_WriteLoRaConfig(cfg);
}

void Adapter_LoadConfig(LoRa_Config_t *cfg) {
    Flash_ReadLoRaConfig(cfg);
}

uint32_t Adapter_GetTick(void) {
    return GetTick();
}

uint32_t Adapter_GetRandomSeed(void) {
    extern uint32_t Port_GetRandomSeed(void);
    return Port_GetRandomSeed();
}

void Adapter_SystemReset(void) {
    printf("[SYS] System Resetting...\r\n");
    Delay_ms(100); // 仅此处允许短暂阻塞以确保打印完成
    NVIC_SystemReset();
}

// [关键] 接收数据回调
void Adapter_OnRecvData(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta) {
    // 1. 打印接收到的数据
    printf("[APP] RX from ID:0x%04X | Len:%d | Payload: %s\r\n", src_id, len, data);
    
    // 2. 业务逻辑: LED 控制
    if (strstr((const char*)data, "LED_ON")) {
        LED2_ON(); 
        printf("    -> Action: LED ON\r\n");
    } else if (strstr((const char*)data, "LED_OFF")) {
        LED2_OFF();
        printf("    -> Action: LED OFF\r\n");
    }

    // 3. [从机模式] 自动回响 (Echo)
#if (TEST_ROLE == 2)
    char reply[64];
    snprintf(reply, 64, "Echo: %s", data);
    
    // 尝试发送回响
    // 注意：如果此时驱动正忙（极低概率，因为刚收完），这里会返回 false
    if (!LoRa_Service_Send((uint8_t*)reply, strlen(reply), src_id)) {
        printf("[APP] Echo Failed: Driver Busy\r\n");
    } else {
        printf("[APP] Echo Sent\r\n");
    }
#endif
}

// [关键] 系统事件回调
void Adapter_OnEvent(LoRa_Event_t event, void *arg) {
    switch(event) {
        case LORA_EVENT_INIT_SUCCESS:
            printf("[EVT] LoRa Init OK. NetID:0x%04X\r\n", g_LoRaConfig_Current.net_id);
            break;
        case LORA_EVENT_MSG_SENT:
            printf("[EVT] TX Complete (Async Callback)\r\n");
            break;
        case LORA_EVENT_MSG_RECEIVED:
            // 物理层收到包的瞬间，LED1 闪一下
            LED1_Turn();
            break;
        default: break;
    }
}

const LoRa_Callback_t my_callbacks = {
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

void Show_Help(void) {
    printf("\r\n=== LoRaPlat V2.3 Async FSM Test ===\r\n");
    printf("Role: %s\r\n", (TEST_ROLE==1)?"HOST":"SLAVE");
    printf("Commands (Type in Serial):\r\n");
    printf("  LED_ON     : Remote LED ON\r\n");
    printf("  LED_OFF    : Remote LED OFF\r\n");
    printf("  (Any text) : Broadcast send\r\n");
    printf("Note: LED1 blinks fast (10Hz) to prove system is NON-BLOCKING.\r\n");
    printf("====================================\r\n");
}

// ============================================================================
// 3. 主函数
// ============================================================================

int main(void)
{
    // 基础硬件初始化
    SysTick_Init();
    LED_Init();
    Serial_Init();
    
    // 打印欢迎信息
    Show_Help();

    // LoRa 服务初始化 (非阻塞，立即返回)
    // 内部会启动 FSM 进行复位，此时 LoRa 模块可能还在拉低 RST，但这里已经返回了
    LoRa_Service_Init(&my_callbacks, 0); 

    uint32_t last_blink = 0;

    while (1)
    {
        // ---------------------------------------------------------
        // 1. 协议栈心跳 (必须高频调用)
        // ---------------------------------------------------------
        // 所有的超时检测、状态跳转、数据接收都在这里发生
        LoRa_Service_Run();

        // ---------------------------------------------------------
        // 2. 业务逻辑: 串口透传 (仅主机)
        // ---------------------------------------------------------
#if (TEST_ROLE == 1)
        if (Serial_RxFlag == 1)
        {
            char *cmd_buf = Serial_RxPacket;
            int len = strlen(cmd_buf);
            // 去除换行符
            while(len > 0 && (cmd_buf[len-1] == '\r' || cmd_buf[len-1] == '\n')) cmd_buf[--len] = '\0';

            if (len > 0) {
                printf("[APP] Request TX: %s\r\n", cmd_buf);
                
                // 调用异步发送接口
                // 如果驱动正忙，这里会立即返回 false，不会死等
                if (!LoRa_Service_Send((uint8_t*)cmd_buf, len, 0xFFFF)) {
                    printf("[APP] Error: System Busy! (Try again later)\r\n");
                }
            }
            Serial_RxFlag = 0; 
        }
#endif

        // ---------------------------------------------------------
        // 3. 验证非阻塞特性: LED 心跳
        // ---------------------------------------------------------
        // 无论 LoRa 是否在发送、复位或等待超时，这个 LED 都应该均匀闪烁
        // 如果 LED 卡顿，说明驱动层存在阻塞代码
        if (GetTick() - last_blink > 50) { // 10Hz 极速闪烁
            last_blink = GetTick();
            LED1_Turn(); 
        }

        // ---------------------------------------------------------
        // 4. 低功耗尝试 (Phase 1 预留)
        // ---------------------------------------------------------
        if (LoRa_Service_IsIdle() && !Serial_RxFlag) {
            // 只有当 LoRa 驱动空闲(IDLE)且串口无数据时，才允许休眠
            // __WFI(); 
        }
    }
}
