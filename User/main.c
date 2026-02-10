#include "stm32f10x.h"
#include "Delay.h"
#include "Serial.h"
#include "LED.h"
#include "lora_app.h"
#include "lora_driver.h" 
#include "lora_manager.h" 
#include "Flash.h"
#include "cJSON.h" 
#include <string.h>
#include <stdio.h>

// ============================================================
//                    测试角色配置
// ============================================================
// 1: 主机 (Host) - ID: 0x0001 (连接电脑)
// 2: 从机 (Slave) - ID: 0x0002 (不接电脑，只接电源)
#define TEST_ROLE       1  // <--- 确保这里是 1

// ============================================================
//                    主机本地调试配置
// ============================================================
#define HOST_FORCE_CH_10    0

volatile uint8_t g_TimeoutFlag; 

// 声明外部函数
extern void App_FactoryReset(void);

// 打印当前 LoRa 参数状态
static void Print_LoRa_Status(void) {
    printf("\r\n=== LoRa Device Status ===\r\n");
    printf("  UUID:          0x%08X\r\n", g_LoRaConfig_Current.uuid);
    printf("  NetID (Logic): 0x%04X %s\r\n", g_LoRaConfig_Current.net_id,
           (g_LoRaConfig_Current.net_id == LORA_ID_UNASSIGNED) ? "(Unassigned)" : "");
    printf("  HWAddr (Phys): 0x%04X\r\n", g_LoRaConfig_Current.hw_addr);
    printf("  Channel:       %d\r\n", g_LoRaConfig_Current.channel);
    printf("  Power:         %d\r\n", g_LoRaConfig_Current.power);
    printf("  Mode:          %d\r\n", g_LoRaConfig_Current.tmode);
    printf("==========================\r\n");
}

// 处理本地配置指令 (仅 Host 使用)
#if (TEST_ROLE == 1)
static void Handle_Local_Command(const char *json_str) {
    cJSON *root = cJSON_Parse(json_str);
    if (!root) return;

    cJSON *cmd_item = cJSON_GetObjectItem(root, "cmd");
    if (cmd_item && strcmp(cmd_item->valuestring, "LOCAL_SET") == 0) {
        printf("[MAIN] Applying Local Config...\r\n");
        
        cJSON *item;
        item = cJSON_GetObjectItem(root, "net_id");
        if (item) g_LoRaConfig_Current.net_id = (uint16_t)item->valuedouble;
        
        item = cJSON_GetObjectItem(root, "hw_addr");
        if (item) g_LoRaConfig_Current.hw_addr = (uint16_t)item->valuedouble;

        item = cJSON_GetObjectItem(root, "ch");
        if (item) g_LoRaConfig_Current.channel = (uint8_t)item->valuedouble;
        
        item = cJSON_GetObjectItem(root, "pwr");
        if (item) g_LoRaConfig_Current.power = (uint8_t)item->valuedouble;
        
        item = cJSON_GetObjectItem(root, "tmode");
        if (item) g_LoRaConfig_Current.tmode = (uint8_t)item->valuedouble;

        Flash_WriteLoRaConfig(&g_LoRaConfig_Current);
        
        extern LoRa_Manager_t g_LoRaManager;
        g_LoRaManager.local_id = g_LoRaConfig_Current.net_id;

        if (Drv_ApplyConfig(&g_LoRaConfig_Current)) {
            Print_LoRa_Status();
        } else {
            printf("[MAIN] Config Failed!\r\n");
        }
    }
    cJSON_Delete(root);
}
#endif

int main(void)
{
    SysTick_Init();
    LED_Init();
    Serial_Init();
    
    // 启动闪烁
    for(int i=0; i<3; i++) { LED1_ON(); Delay_ms(100); LED1_OFF(); Delay_ms(100); }

    printf("\r\n=========================================\r\n");
    #if (TEST_ROLE == 1)
        printf("      [MODE] HOST (ID: 0x0001)           \r\n");
        uint16_t my_id = 0x0001;
    #else
        printf("      [MODE] SLAVE (ID: 0x0002)          \r\n");
        uint16_t my_id = 0x0002;
    #endif
    printf("=========================================\r\n");

    // 初始化 LoRa
    LoRa_App_Init(my_id);
    Print_LoRa_Status();

    printf("[SYS] Loop Start. Waiting for commands...\r\n");
    printf("------------------------------------------------\r\n");
    printf(" Test Commands Guide:\r\n");
    printf(" 1. CMD0 <json>  -> Send to ID 0 (Unassigned)\r\n");
    printf("    Ex: CMD0 {\"cmd\":\"LED_ON\"}\r\n");
    printf(" 2. BIND <uuid> <id> -> Bind UUID to new ID\r\n");
    printf("    Ex: BIND 3685384454 5\r\n");
    printf(" 3. CMD5 <json>  -> Send to ID 5\r\n");
    printf("    Ex: CMD5 {\"cmd\":\"LED_OFF\"}\r\n");
    printf(" 4. CMD2 <json>  -> Send to ID 2 (Test Filter)\r\n");
    printf("    Ex: CMD2 {\"cmd\":\"LED_ON\"}\r\n");
    printf("------------------------------------------------\r\n");

    while (1)
    {
        LoRa_App_Task();

        #if (TEST_ROLE == 1)
        if (Serial_RxFlag == 1)
        {
            char *input_str = Serial_RxPacket;
            uint16_t len = strlen(input_str);
            // 去除回车换行
            while(len > 0 && (input_str[len-1] == '\r' || input_str[len-1] == '\n')) {
                input_str[--len] = '\0';
            }

            if (len > 0)
            {
                printf("[HOST] Input: %s\r\n", input_str);

                // --- 场景 1: 测试未分配设备 (发给 ID 0) ---
                if (strncmp(input_str, "CMD0", 4) == 0) {
                    printf(" -> Sending to ID 0x0000...\r\n");
                    LED1_ON();
                    // 跳过前5个字符("CMD0 "), 发送后面的 JSON
                    Manager_SendPacket((uint8_t*)(input_str + 5), strlen(input_str) - 5, 0x0000);
                    LED1_OFF();
                }
                
                // --- 场景 2: 执行绑定 (发给 ID 0) ---
                else if (strncmp(input_str, "BIND", 4) == 0) {
                    uint32_t u; 
                    int id;
                    // 解析 UUID 和 新ID
                    if (sscanf(input_str + 5, "%u %d", &u, &id) == 2) {
                        char buf[128];
                        sprintf(buf, "{\"cmd\":\"CFG_BIND\",\"uuid\":%u,\"new_id\":%d}", u, id);
                        printf(" -> Binding UUID %u to ID %d...\r\n", u, id);
                        printf(" -> Payload: %s\r\n", buf);
                        LED1_ON();
                        Manager_SendPacket((uint8_t*)buf, strlen(buf), 0x0000);
                        LED1_OFF();
                    } else {
                        printf(" -> BIND Error: Invalid Format. Use: BIND <uuid> <id>\r\n");
                    }
                }

                // --- 场景 3: 控制已绑定设备 (发给 ID 5) ---
                else if (strncmp(input_str, "CMD5", 4) == 0) {
                    printf(" -> Sending to ID 0x0005...\r\n");
                    LED1_ON();
                    Manager_SendPacket((uint8_t*)(input_str + 5), strlen(input_str) - 5, 0x0005);
                    LED1_OFF();
                }

                // --- 场景 4: 测试过滤 (发给 ID 2) ---
                else if (strncmp(input_str, "CMD2", 4) == 0) {
                    printf(" -> Sending to ID 0x0002...\r\n");
                    LED1_ON();
                    Manager_SendPacket((uint8_t*)(input_str + 5), strlen(input_str) - 5, 0x0002);
                    LED1_OFF();
                }
                
                // --- 本地配置指令 ---
                else if (strstr(input_str, "LOCAL_SET")) {
                    Handle_Local_Command(input_str);
                }
                else if (strstr(input_str, "LOCAL_RESET")) {
                    App_FactoryReset();
                }
                
                // --- 默认透传 ---
                else {
                    printf(" -> Unknown CMD. Sending raw to ID 0xFFFF (Broadcast)...\r\n");
                    Manager_SendPacket((uint8_t*)input_str, len, 0xFFFF);
                }
            }
            Serial_RxFlag = 0;
        }
        #endif
        
        // Slave 心跳 (仅在 Slave 模式下编译)
        #if (TEST_ROLE == 2)
            static uint32_t last_tick = 0;
            if (GetTick() - last_tick > 2000) {
                // LED1_ON(); Delay_ms(10); LED1_OFF(); // 可选心跳
                last_tick = GetTick();
            }
        #endif
    }
}
