#include "lora_app.h"
#include "lora_manager.h"
#include "Serial.h"
#include "Delay.h"
#include "LED.h"
#include <stdio.h>
#include <string.h>

// ============================================================
//                    私有变量
// ============================================================

static volatile bool s_app_busy = false; // 应用层忙碌标志 (流控)
static uint32_t s_last_send_tick = 0;
static uint8_t  s_toggle_state = 0;      // 用于主机切换发送内容

// ============================================================
//                    回调函数实现
// ============================================================

// [通用] 接收数据回调
static void _App_OnRxData(uint8_t *data, uint16_t len, uint16_t src_id)
{
    // 1. 打印原始数据
    // 为了安全打印，拷贝到临时buffer并加结束符
    char temp_buf[64];
    uint16_t copy_len = (len > 63) ? 63 : len;
    memcpy(temp_buf, data, copy_len);
    temp_buf[copy_len] = '\0';
    
    printf("\r\n[APP] Rx from 0x%04X: %s\r\n", src_id, temp_buf);
    
    // 2. 业务解析 (仅 SLAVE 需要解析指令)
#if (LORA_APP_ROLE == 0)
    // 简单的字符串匹配模拟 JSON 解析
    if (strstr(temp_buf, "LED_ON"))
    {
        printf("[APP] Action: LED ON\r\n");
        LED1_ON();
    }
    else if (strstr(temp_buf, "LED_OFF"))
    {
        printf("[APP] Action: LED OFF\r\n");
        LED1_OFF();
    }
    else
    {
        printf("[APP] Unknown Command\r\n");
    }
#endif
}

// [通用] 发送结果回调
static void _App_OnTxResult(bool success)
{
    s_app_busy = false; // 释放忙碌标志，允许下一次发送
    
    if (success) {
        printf("[APP] TX Finished (Success/ACKed)\r\n");
        // 发送成功闪烁一下 LED
        LED1_ON(); Delay_ms(50); LED1_OFF();
    } else {
        printf("[APP] TX Failed (Timeout)\r\n");
    }
}

// [通用] 错误回调
static void _App_OnError(LoRaError_t err)
{
    // 严重错误时，也可以释放忙碌标志，防止死锁
    if (err == LORA_ERR_TX_TIMEOUT || err == LORA_ERR_ACK_TIMEOUT) {
        s_app_busy = false;
    }
    printf("[APP] System Error: %d\r\n", err);
}

// ============================================================
//                    公共接口
// ============================================================

void LoRa_App_Init(void)
{
    ManagerConfig_t cfg;
    
    // 根据角色配置 ID
#if (LORA_APP_ROLE == 1)
    cfg.local_id = HOST_LOCAL_ID;
    printf("[APP] Role: HOST (Controller)\r\n");
#else
    cfg.local_id = SLAVE_LOCAL_ID;
    printf("[APP] Role: SLAVE (Executor)\r\n");
#endif

    cfg.enable_crc = true;
    cfg.enable_ack = true; // 开启可靠传输
    
    // 初始化 Manager
    Manager_Init(&cfg, _App_OnRxData, _App_OnTxResult, _App_OnError);
    
    s_app_busy = false;
    s_last_send_tick = GetTick();
}

void LoRa_App_Task(void)
{
    // 1. 驱动协议栈 (必须高频调用)
    Manager_Run();
    
    // 2. 主机发送逻辑
#if (LORA_APP_ROLE == 1)
    
    // 简单的流控：只有当 (非忙碌) AND (时间间隔到达) 时才发送
    if (!s_app_busy && (GetTick() - s_last_send_tick > APP_SEND_INTERVAL))
    {
        s_last_send_tick = GetTick();
        
        char payload[32];
        if (s_toggle_state == 0) {
            sprintf(payload, "{\"cmd\":\"LED_ON\"}");
            s_toggle_state = 1;
        } else {
            sprintf(payload, "{\"cmd\":\"LED_OFF\"}");
            s_toggle_state = 0;
        }
        
        printf("\r\n[APP] Sending: %s\r\n", payload);
        
        // 发送请求
        bool res = Manager_SendPacket((uint8_t*)payload, strlen(payload), HOST_TARGET_ID);
        
        if (res) {
            s_app_busy = true; // 标记忙碌，等待 OnTxResult 回调
        } else {
            printf("[APP] Send Rejected (Buffer Full?)\r\n");
        }
    }
    
#endif
}
