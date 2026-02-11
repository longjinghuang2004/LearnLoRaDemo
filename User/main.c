#include "stm32f10x.h"
#include "Delay.h"
#include "Serial.h"
#include "LED.h"
#include "Flash.h"
#include "lora_service.h" 
#include "cJSON.h" 
#include <string.h>
#include <stdio.h>

// ============================================================================
// [测试角色配置] - 编译前请修改此处
// ============================================================================
// 1 = 主机 (HOST): 连接PC串口助手，用于发送指令，解析串口输入
// 2 = 从机 (NODE): 独立运行，响应指令，定期发送心跳
// ============================================================================
#define TEST_ROLE      1  

volatile uint8_t g_TimeoutFlag;

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
    Delay_ms(100);
    NVIC_SystemReset();
}

// 收到数据时的回调
void Adapter_OnRecvData(uint16_t src_id, const uint8_t *data, uint16_t len) {
    // 打印接收到的原始数据
    printf("[APP] RX from ID:0x%04X | Len:%d | Payload: %s\r\n", src_id, len, data);
    
    // LED 动作指示
    LED1_ON(); 
    Delay_ms(50); 
    LED1_OFF();

    // 简单的业务指令解析
    if (strstr((const char*)data, "LED_ON")) {
        LED2_ON(); // 假设板子上有LED2，或者复用LED1长亮
        printf("    -> Action: LED ON\r\n");
    }
    else if (strstr((const char*)data, "LED_OFF")) {
        LED2_OFF();
        printf("    -> Action: LED OFF\r\n");
    }
}

// LoRa 事件回调
void Adapter_OnEvent(LoRa_Event_t event, void *arg) {
    switch(event) {
        case LORA_EVENT_INIT_SUCCESS:
            printf("[EVT] Init OK. UUID:0x%08X, NetID:0x%04X\r\n", 
                   g_LoRaConfig_Current.uuid, g_LoRaConfig_Current.net_id);
            break;
        case LORA_EVENT_BIND_SUCCESS:
            printf("[EVT] BIND OK! New NetID: %d\r\n", *(uint16_t*)arg);
            // 绑定成功后闪烁提示
            for(int i=0; i<5; i++) { LED1_Turn(); Delay_ms(100); }
            break;
        case LORA_EVENT_MSG_RECEIVED:
            // 收到物理层包时的瞬间闪烁（调试用）
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
    printf("\r\n=== LoRa Command Help ===\r\n");
    printf("1. CMD0 <msg>       : Send to ID 0 (Unassigned)\r\n");
    printf("2. CMD <id> <msg>   : Send to specific ID (e.g., CMD 5 HELLO)\r\n");
    printf("3. BROADCAST <msg>  : Send to All (ID 0xFFFF)\r\n");
    printf("4. BIND <uuid> <id> : Bind device with UUID to new ID\r\n");
    printf("5. HELP             : Show this menu\r\n");
    printf("   (Any other text) : Default broadcast\r\n");
    printf("=========================\r\n");
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
    
    printf("\r\n=== LoRaPlat v2.0 Test Mode: %s ===\r\n", (TEST_ROLE==1)?"HOST":"NODE");

    // LoRa 服务初始化
    // 传入 0 表示优先使用 Flash 中存储的 ID
    LoRa_Service_Init(&my_callbacks, 0); 

#if (TEST_ROLE == 1)
    Show_Help();
#endif

    while (1)
    {
        // [核心] 驱动服务层状态机
        LoRa_Service_Run();

        // --------------------------------------------------------------------
        // Role 1: 主机模式 (处理串口输入 -> 发送 LoRa)
        // --------------------------------------------------------------------
#if (TEST_ROLE == 1)
        if (Serial_RxFlag == 1)
        {
            char *cmd_buf = Serial_RxPacket;
            // 清理末尾换行符
            int len = strlen(cmd_buf);
            while(len > 0 && (cmd_buf[len-1] == '\r' || cmd_buf[len-1] == '\n')) {
                cmd_buf[--len] = '\0';
            }

            if (len > 0) {
                printf("龙 Input: %s\r\n", cmd_buf);

                // --- 指令 1: 帮助 ---
                if (strcasecmp(cmd_buf, "HELP") == 0) {
                    Show_Help();
                }
                // --- 指令 2: 发给 ID 0 (默认未配置设备) ---
                else if (strncmp(cmd_buf, "CMD0 ", 5) == 0) {
                    printf(" -> Sending to ID 0x0000...\r\n");
                    LoRa_Service_Send((uint8_t*)(cmd_buf + 5), strlen(cmd_buf) - 5, 0x0000);
                }
                // --- 指令 3: 发给任意 ID ---
                else if (strncmp(cmd_buf, "CMD ", 4) == 0) {
                    int target_id;
                    char msg[64];
                    if (sscanf(cmd_buf + 4, "%d %[^\n]", &target_id, msg) == 2) {
                        printf(" -> Sending to ID %d: %s\r\n", target_id, msg);
                        LoRa_Service_Send((uint8_t*)msg, strlen(msg), (uint16_t)target_id);
                    } else {
                        printf(" -> Error: Format is CMD <id> <msg>\r\n");
                    }
                }
                // --- 指令 4: 广播 ---
                else if (strncmp(cmd_buf, "BROADCAST ", 10) == 0) {
                    printf(" -> Broadcasting...\r\n");
                    LoRa_Service_Send((uint8_t*)(cmd_buf + 10), strlen(cmd_buf) - 10, 0xFFFF);
                }
                // --- 指令 5: 绑定操作 ---
                else if (strncmp(cmd_buf, "BIND ", 5) == 0) {
                    uint32_t u;
                    int id;
                    if (sscanf(cmd_buf + 5, "%u %d", &u, &id) == 2) {
                        char json[128];
                        // 构造 CFG_BIND 指令
                        sprintf(json, "{\"cmd\":\"CFG_BIND\",\"uuid\":%u,\"new_id\":%d}", u, id);
                        printf(" -> Sending Bind Request: %s\r\n", json);
                        // 绑定指令通常发给 ID 0 (未分配) 或 广播
                        LoRa_Service_Send((uint8_t*)json, strlen(json), 0x0000);
                    } else {
                        printf(" -> Error: Format is BIND <uuid> <new_id>\r\n");
                    }
                }
                // --- 默认: 广播透传 ---
                else {
                    printf(" -> Raw Broadcast (ID 0xFFFF)...\r\n");
                    LoRa_Service_Send((uint8_t*)cmd_buf, len, 0xFFFF);
                }
            }
            // 清除接收标志，准备下一次接收
            Serial_RxFlag = 0; 
        }
#endif

        // --------------------------------------------------------------------
        // Role 2: 从机模式 (定期发送心跳)
        // --------------------------------------------------------------------
#if (TEST_ROLE == 2)
        static uint32_t last_heartbeat = 0;
        if (GetTick() - last_heartbeat > 3000) { // 每3秒一次
            last_heartbeat = GetTick();
            
            char beat_msg[32];
            sprintf(beat_msg, "Heartbeat from 0x%04X", g_LoRaConfig_Current.net_id);
            
            // 发给主机 (假设主机 ID 为 1，或者广播)
            // 这里演示发给广播，方便主机看到
            LoRa_Service_Send((uint8_t*)beat_msg, strlen(beat_msg), 0xFFFF);
            
            LED1_Turn(); // 闪灯指示活着
        }
#endif
    }
}
