#include "lora_app.h"
#include "lora_manager.h"
#include "Serial.h"
#include "Delay.h"
#include "LED.h"
#include <stdio.h>
#include <string.h>

static volatile bool s_app_busy = false;
static uint32_t s_last_send_tick = 0;
static uint8_t  s_toggle_state = 0;

static void _App_OnRxData(uint8_t *data, uint16_t len, uint16_t src_id) {
    char temp_buf[64];
    uint16_t copy_len = (len > 63) ? 63 : len;
    memcpy(temp_buf, data, copy_len);
    temp_buf[copy_len] = '\0';
    
    printf("\r\n[APP] Rx from 0x%04X: %s\r\n", src_id, temp_buf);
    
#if (LORA_APP_ROLE == 0) // Slave
    if (strstr(temp_buf, "LED_ON")) {
        printf("[APP] Action: LED ON\r\n");
        LED1_ON();
    } else if (strstr(temp_buf, "LED_OFF")) {
        printf("[APP] Action: LED OFF\r\n");
        LED1_OFF();
    }
#endif
}

static void _App_OnTxResult(bool success) {
    s_app_busy = false;
    if (success) {
        printf("[APP] TX Finished (Success/ACKed)\r\n");
        LED1_ON(); Delay_ms(50); LED1_OFF();
    } else {
        printf("[APP] TX Failed (Timeout)\r\n");
    }
}

static void _App_OnError(LoRaError_t err) {
    if (err == LORA_ERR_TX_TIMEOUT || err == LORA_ERR_ACK_TIMEOUT) {
        s_app_busy = false;
    }
    printf("[APP] System Error: %d\r\n", err);
}

void LoRa_App_Init(void) {
    // 1. 初始化 Manager (此时 ID 是默认值)
    Manager_Init(_App_OnRxData, _App_OnTxResult, _App_OnError);
    
    // 2. 强制覆盖 ID
#if (LORA_APP_ROLE == 1)
    g_LoRaManager.local_id = 0x0001;
    printf("[APP] Role: HOST (ID: 0x%04X)\r\n", g_LoRaManager.local_id);
#else
    g_LoRaManager.local_id = 0x0002; // 确保从机是 0x0002
    printf("[APP] Role: SLAVE (ID: 0x%04X)\r\n", g_LoRaManager.local_id);
#endif

    s_app_busy = false;
    s_last_send_tick = GetTick();
}

void LoRa_App_Task(void) {
    Manager_Run();
    
#if (LORA_APP_ROLE == 1) // Host Logic
    if (!s_app_busy && (GetTick() - s_last_send_tick > APP_SEND_INTERVAL)) {
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
        if (Manager_SendPacket((uint8_t*)payload, strlen(payload), HOST_TARGET_ID)) {
            s_app_busy = true;
        }
    }
#endif
}
