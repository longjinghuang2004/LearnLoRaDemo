/**
  ******************************************************************************
  * @file    main.c
  * @author  LoRaPlat Team
  * @brief   LoRaPlat V3.3.3 功能验证测试 (交互式终端)
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
#include <stdlib.h> // for atoi

volatile uint8_t g_TimeoutFlag;

extern void Demo_OSAL_Init(void); 

// ============================================================================
// [测试角色配置]
// 1 = HOST (主机): 发起测试指令
// 2 = SLAVE (从机): 被动响应
// ============================================================================
#define TEST_ROLE      2

// ============================================================================
// 全局变量
// ============================================================================
static uint32_t g_TxSeq = 0; // 发送序号计数器

// ============================================================================
// 1. 接口适配 (Adapter Layer)
// ============================================================================

void Adapter_SaveConfig(const LoRa_Config_t *cfg) {
    Flash_WriteLoRaConfig(cfg);
    Serial_Printf("[APP] Config Saved to Flash.\r\n");
}

void Adapter_LoadConfig(LoRa_Config_t *cfg) {
    Flash_ReadLoRaConfig(cfg);
    // 如果 Flash 无效 (Magic 不对)，Service 层会自动加载默认值
    // 这里我们打印一下读取结果
    if (cfg->magic == LORA_CFG_MAGIC) {
        Serial_Printf("[APP] Config Loaded: ID=%d, Group=%d\r\n", cfg->net_id, cfg->group_id);
    } else {
        Serial_Printf("[APP] Flash Empty, using defaults.\r\n");
    }
}

uint32_t Adapter_GetRandomSeed(void) {
    return LoRa_Port_GetEntropy32(); 
}

void Adapter_SystemReset(void) {
    Serial_Printf("[APP] System Resetting...\r\n");
    Delay_ms(100); 
    NVIC_SystemReset();
}

// 接收数据回调
void Adapter_OnRecvData(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta) {
    // 打印接收到的数据
    Serial_Printf("[APP] RX from 0x%04X (RSSI:%d): %.*s\r\n", src_id, meta->rssi, len, data);
    
    // LED 指示
    LED2_Turn();
}

// 事件回调
void Adapter_OnEvent(LoRa_Event_t event, void *arg) {
    switch(event) {
        case LORA_EVENT_INIT_SUCCESS:
            Serial_Printf("[EVT] Init Success.\r\n");
            break;
            
        case LORA_EVENT_MSG_SENT:
            // 物理层发送完成 (DMA 结束)
            // Serial_Printf("[EVT] PHY TX Done.\r\n"); 
            break;
            
        case LORA_EVENT_TX_FINISHED:
            // 逻辑层发送完成 (收到 ACK 或 广播结束)
            Serial_Printf("[EVT] TX FINISHED (Success)\r\n");
            LED1_Turn(); // 成功指示
            break;
            
        case LORA_EVENT_TX_FAILED:
            // 重传失败
            Serial_Printf("[EVT] TX FAILED (Timeout)\r\n");
            // 可以在这里触发报警或重试逻辑
            break;
            
        case LORA_EVENT_CONFIG_COMMIT:
            Serial_Printf("[EVT] Config Commit.\r\n");
            break;
            
        case LORA_EVENT_FACTORY_RESET:
            Serial_Printf("[EVT] Factory Reset.\r\n");
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

// 首次运行初始化 Flash
void Check_First_Run(void) {
    LoRa_Config_t cfg;
    Flash_ReadLoRaConfig(&cfg);
    
    if (cfg.magic != LORA_CFG_MAGIC) {
        Serial_Printf("[APP] First Run, Init Flash...\r\n");
        // 设置默认参数
        memset(&cfg, 0, sizeof(cfg));
        cfg.magic = LORA_CFG_MAGIC;
        cfg.net_id = (TEST_ROLE == 1) ? 1 : 2; // Host=1, Slave=2
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

// 解析测试指令
void Process_Test_Command(char *cmd_str) {
    char *cmd = strtok(cmd_str, ":");
    char *params = strtok(NULL, "");
    
    if (cmd == NULL) return;
    
    // 1. TEST 指令
    if (strcmp(cmd, "TEST") == 0 && params != NULL) {
        char msg[32];
        
        // TEST:UNI -> 发送单播包
        if (strcmp(params, "UNI") == 0) {
            sprintf(msg, "UNI-SEQ-%d", ++g_TxSeq);
            uint16_t target = (TEST_ROLE == 1) ? 2 : 1;
            Serial_Printf(" -> Sending Unicast to %d: %s\r\n", target, msg);
            
            if (!LoRa_Service_Send((uint8_t*)msg, strlen(msg), target)) {
                Serial_Printf(" -> Send Failed: Busy\r\n");
            }
        }
        // TEST:BCAST -> 发送广播包
        else if (strcmp(params, "BCAST") == 0) {
            sprintf(msg, "BCAST-SEQ-%d", ++g_TxSeq);
            Serial_Printf(" -> Sending Broadcast: %s\r\n", msg);
            
            if (!LoRa_Service_Send((uint8_t*)msg, strlen(msg), 0xFFFF)) {
                Serial_Printf(" -> Send Failed: Busy\r\n");
            }
        }
    }
    // 2. CMD 指令 (平台配置)
    else if (strcmp(cmd, "CMD") == 0 && params != NULL) {
        // 重构 CMD 字符串以适配 LoRa_Service_Command_Process
        // 因为 strtok 破坏了原字符串，我们需要重新拼接 "CMD:..."
        // 或者直接把 params 传给 Command 模块？
        // Command 模块期望的是 "BIND=..." 这种格式，不带 "CMD:" 前缀？
        // 查看 lora_service_command.c: 它期望 "BIND=..."
        // 但 main.c 里的 input_buf 是 "CMD:BIND=..."
        // 所以我们直接传 params 即可。
        
        // 修正：lora_service_command.c 内部再次 strtok，所以传入 "BIND=..." 是安全的
        // 但为了兼容性，我们构造一个临时 buffer
        char full_cmd[64];
        snprintf(full_cmd, sizeof(full_cmd), "%s", params); // params = "BIND=..."
        
        // 调用 Service 层处理
        // 注意：lora_service_command.c 需要暴露给 main 吗？
        // 通常 main 只调用 Service_Run/Send。
        // 这里为了测试方便，我们假设 main 可以调用 Command 模块，或者 Service 提供透传接口。
        // 暂时直接调用内部解析逻辑 (需要 include lora_service_command.h)
        // 但为了架构整洁，我们模拟接收到了一个 CMD 包
        
        // 【Hack】直接调用 Command Process
        // 需要在 main.c 头部 include "lora_service_command.h"
        // 这里为了不修改 include，我们手动声明一下
        extern bool LoRa_Service_Command_Process(char *cmd_str);
        
        if (LoRa_Service_Command_Process(full_cmd)) {
            Serial_Printf(" -> Command Processed.\r\n");
        } else {
            Serial_Printf(" -> Command Unknown.\r\n");
        }
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
    
    // 检查 Flash 初始化
    Check_First_Run();
    
    NVIC_PriorityGroupConfig(NVIC_PriorityGroup_2);
    
    Serial_Printf("\r\n=== LoRaPlat V3.7 Test Console (Role: %d) ===\r\n", TEST_ROLE);
    Serial_Printf("Commands:\r\n");
    Serial_Printf("  TEST:UNI   - Send Unicast\r\n");
    Serial_Printf("  TEST:BCAST - Send Broadcast\r\n");
    Serial_Printf("  CMD:BIND=uuid,id - Change ID\r\n");
    Serial_Printf("  CMD:RST    - Reboot\r\n");
    Serial_Printf("=============================================\r\n");

    LoRa_Service_Init(&my_adapter, 0); 

    char input_buf[128];
    uint32_t last_blink = 0;

    while (1)
    {
        // 1. 协议栈驱动
        LoRa_Service_Run();

        // 2. 串口指令处理
        if (Serial_GetRxPacket(input_buf, sizeof(input_buf))) {
            Serial_Printf("[PC] Input: %s\r\n", input_buf);
            Process_Test_Command(input_buf);
        }

        // 3. LED 心跳
        if (GetTick() - last_blink > 500) { 
            last_blink = GetTick();
            LED1_Turn(); 
        }
    }
}
