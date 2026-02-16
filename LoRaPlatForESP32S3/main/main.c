#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h" // for esp_restart
#include "nvs_flash.h"
#include "driver/uart.h"

// 引入组件头文件
#include "bsp_led.h"

// 引入 LoRaPlat (只引入 Service 层)
#include "lora_service.h"
#include "lora_port.h"
#include "lora_service_command.h"

static const char *TAG = "MAIN";

// --- 引脚定义 ---
#define PIN_WS2812      48
#define PIN_BUTTON      0 

// --- 声明外部初始化函数 ---
extern void LoRa_OSAL_Init_ESP32(void);

// ============================================================
//                    NVS 存储适配
// ============================================================
#define NVS_NAMESPACE "lora_store"
#define NVS_KEY_CFG   "sys_cfg"

static void App_SaveConfig(const LoRa_Config_t *cfg) {
    nvs_handle_t my_handle;
    esp_err_t err;
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS Open Failed");
        return;
    }
    nvs_set_blob(my_handle, NVS_KEY_CFG, cfg, sizeof(LoRa_Config_t));
    nvs_commit(my_handle);
    nvs_close(my_handle);
    ESP_LOGI(TAG, "Config Saved to NVS");
}

static void App_LoadConfig(LoRa_Config_t *cfg) {
    nvs_handle_t my_handle;
    esp_err_t err;
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        size_t required_size = sizeof(LoRa_Config_t);
        nvs_get_blob(my_handle, NVS_KEY_CFG, cfg, &required_size);
        ESP_LOGI(TAG, "Config Loaded from NVS");
    } else {
        ESP_LOGW(TAG, "NVS Empty, using defaults");
    }
    nvs_close(my_handle);
}

static void App_SystemReset(void) {
    ESP_LOGW(TAG, "System Restarting...");
    esp_restart();
}

// ============================================================
//                    1. LoRa 回调逻辑
// ============================================================

static void App_OnRecvData(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta) {
    ESP_LOGI(TAG, "[RX] From 0x%04X (RSSI:%d): %.*s", src_id, meta->rssi, len, data);

    char cmd_buf[64];
    uint16_t copy_len = (len < sizeof(cmd_buf) - 1) ? len : (sizeof(cmd_buf) - 1);
    memcpy(cmd_buf, data, copy_len);
    cmd_buf[copy_len] = '\0';

    // 执行动作 (LED 控制)
    if (strncmp(cmd_buf, "red", 3) == 0) BSP_LED_SetColor(50, 0, 0);
    else if (strncmp(cmd_buf, "blue", 4) == 0) BSP_LED_SetColor(0, 0, 50);
    else if (strncmp(cmd_buf, "off", 3) == 0) BSP_LED_SetColor(0, 0, 0);

    // 全量回传 (Echo)
    // [修改] 捕获 MsgID
    LoRa_MsgID_t msg_id = LoRa_Service_Send(data, len, src_id, LORA_OPT_CONFIRMED);
    if (msg_id > 0) {
        ESP_LOGI(TAG, "Echo Enqueued (ID:%d)", msg_id);
    } else {
        ESP_LOGW(TAG, "Echo failed (Busy)");
    }
}

static void App_OnEvent(LoRa_Event_t event, void *arg) {
    switch(event) {
        case LORA_EVENT_INIT_SUCCESS: 
            ESP_LOGI(TAG, "LoRa Stack Ready."); 
            BSP_LED_SetColor(0, 20, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            BSP_LED_SetColor(0, 0, 0);
            break;
            
        // [新增] 处理带 ID 的成功事件
        case LORA_EVENT_TX_SUCCESS_ID:
            ESP_LOGI(TAG, "Msg ID:%d Send Success (ACKed)", *(LoRa_MsgID_t*)arg);
            break;
            
        // [新增] 处理带 ID 的失败事件
        case LORA_EVENT_TX_FAILED_ID:
            ESP_LOGW(TAG, "Msg ID:%d Send Failed (Timeout)", *(LoRa_MsgID_t*)arg);
            break;
            
        case LORA_EVENT_CONFIG_COMMIT:
            ESP_LOGI(TAG, "Config Commit Event Triggered.");
            break;
            
        default: break;
    }
}

static const LoRa_Callback_t s_LoRaCb = {
    .SaveConfig = App_SaveConfig,
    .LoadConfig = App_LoadConfig,
    .GetRandomSeed = NULL, 
    .SystemReset = App_SystemReset,
    .OnRecvData = App_OnRecvData,
    .OnEvent = App_OnEvent
};

// ============================================================
//                    2. 任务定义
// ============================================================

void lora_task_entry(void *arg) {
    ESP_LOGI(TAG, "LoRa Task Started");
    while (1) {
        LoRa_Service_Run();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

void console_task_entry(void *arg) {
    char line[128];
    char resp_buf[128]; 

    printf("\n=== ESP32 LoRaPlat Shell ===\n");

    while (1) {
        if (fgets(line, sizeof(line), stdin) != NULL) {
            char *pos = strchr(line, '\n');
            if (pos) *pos = '\0';
            pos = strchr(line, '\r');
            if (pos) *pos = '\0';

            if (strlen(line) > 0) {
                printf("[Shell] Input: %s\n", line);

                if (strncmp(line, "CMD:", 4) == 0) {
                    if (LoRa_Service_Command_Process(line, resp_buf, sizeof(resp_buf))) {
                        printf(" -> CMD Result: %s\n", resp_buf);
                    } else {
                        printf(" -> CMD Failed\n");
                    }
                }
                else {
                    // [修改] 捕获 MsgID 并打印
                    LoRa_MsgID_t msg_id = LoRa_Service_Send((uint8_t*)line, strlen(line), 0x0001, LORA_OPT_CONFIRMED);
                    if (msg_id > 0) {
                        printf(" -> Enqueued ID:%d (Confirmed)...\n", msg_id);
                    } else {
                        printf(" -> Send Failed (Busy)\n");
                    }
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50));
    }
}

// ============================================================
//                    3. 主入口
// ============================================================
void app_main(void)
{
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    BSP_LED_Init(PIN_WS2812);
    LoRa_OSAL_Init_ESP32();
    
    // 初始化协议栈
    LoRa_Service_Init(&s_LoRaCb, 0x0002); 

    xTaskCreate(lora_task_entry, "lora_task", 4096, NULL, 5, NULL);
    xTaskCreate(console_task_entry, "console_task", 4096, NULL, 3, NULL);
}
