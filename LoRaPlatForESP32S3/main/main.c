#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/uart.h"

// 引入组件头文件
#include "bsp_led.h"
// #include "KeyManager.h" // 如果没用到可以注释掉
// #include "Key.h"

// 引入 LoRaPlat
#include "lora_service.h"
#include "lora_port.h"
#include "lora_service_command.h"

static const char *TAG = "MAIN";

// --- 引脚定义 ---
#define PIN_WS2812      48
#define PIN_BUTTON      21

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
    if (err != ESP_OK) return;
    nvs_set_blob(my_handle, NVS_KEY_CFG, cfg, sizeof(LoRa_Config_t));
    nvs_commit(my_handle);
    nvs_close(my_handle);
}

static void App_LoadConfig(LoRa_Config_t *cfg) {
    nvs_handle_t my_handle;
    esp_err_t err;
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err == ESP_OK) {
        size_t required_size = sizeof(LoRa_Config_t);
        nvs_get_blob(my_handle, NVS_KEY_CFG, cfg, &required_size);
        ESP_LOGI(TAG, "Config loaded from NVS.");
    }
    nvs_close(my_handle);
}

// ============================================================
//                    1. LoRa 回调逻辑 (核心修改区)
// ============================================================

static void App_OnRecvData(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta) {
    // 1. 打印接收到的数据
    ESP_LOGI(TAG, "[RX] From 0x%04X (RSSI:%d): %.*s", src_id, meta->rssi, len, data);

    char cmd_buf[64];
    uint16_t copy_len = (len < sizeof(cmd_buf) - 1) ? len : (sizeof(cmd_buf) - 1);
    memcpy(cmd_buf, data, copy_len);
    cmd_buf[copy_len] = '\0';

    // 2. 执行动作 (LED 控制)
    if (strncmp(cmd_buf, "red", 3) == 0) {
        BSP_LED_SetColor(50, 0, 0);
    } 
    else if (strncmp(cmd_buf, "blue", 4) == 0 || strncmp(cmd_buf, "bule", 4) == 0) { 
        BSP_LED_SetColor(0, 0, 50);
    } 
    else if (strncmp(cmd_buf, "white", 5) == 0) {
        BSP_LED_SetColor(20, 20, 20);
    } 
    else if (strncmp(cmd_buf, "off", 3) == 0) {
        BSP_LED_SetColor(0, 0, 0);
    }

    // 3. [关键修改] 全量回传 (Echo)
    // 使用 LORA_OPT_CONFIRMED，强制要求 STM32 收到回传后，给 ESP32 回一个 ACK
    // 这样就构成了完整的四次握手：
    // STM32->ESP32 (Data) | ESP32->STM32 (ACK) | ESP32->STM32 (Echo Data) | STM32->ESP32 (ACK)
    bool res = LoRa_Service_Send(data, len, src_id, LORA_OPT_CONFIRMED);
    
    if (res) {
        ESP_LOGI(TAG, "Echo Sent (Confirmed)");
    } else {
        ESP_LOGW(TAG, "Echo failed (Queue Full or Busy)");
    }
}

static void App_OnEvent(LoRa_Event_t event, void *arg) {
    switch(event) {
        case LORA_EVENT_INIT_SUCCESS: 
            ESP_LOGI(TAG, "LoRa Init Success!"); 
            BSP_LED_SetColor(0, 20, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            BSP_LED_SetColor(0, 0, 0);
            break;
        case LORA_EVENT_TX_FINISHED:
            ESP_LOGD(TAG, "TX Finished (ACK OK)"); // 收到 STM32 对 Echo 的 ACK
            break;
        case LORA_EVENT_TX_FAILED:
            ESP_LOGW(TAG, "TX Failed (Timeout)");
            break;
        case LORA_EVENT_BIND_SUCCESS:
            ESP_LOGI(TAG, "Bind Success! New NetID: %d", *(uint16_t*)arg);
            break;
        case LORA_EVENT_CONFIG_COMMIT:
            ESP_LOGI(TAG, "Config Saved to Flash.");
            break;
        default: break;
    }
}

static const LoRa_Callback_t s_LoRaCb = {
    .SaveConfig = App_SaveConfig,
    .LoadConfig = App_LoadConfig,
    .GetRandomSeed = NULL,
    .SystemReset = NULL,
    .OnRecvData = App_OnRecvData,
    .OnEvent = App_OnEvent
};

// ============================================================
//                    2. 任务定义
// ============================================================

// --- LoRa 协议栈驱动任务 ---
void lora_task_entry(void *arg) {
    ESP_LOGI(TAG, "LoRa Task Started");
    while (1) {
        LoRa_Service_Run();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// --- PC 串口控制台任务 ---
void console_task_entry(void *arg) {
    char line[128];
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
                    if (LoRa_Service_Command_Process(line)) {
                        printf(" -> Command Executed OK.\n");
                    } else {
                        printf(" -> Command Failed.\n");
                    }
                }
                else {
                    printf(" -> Sending to STM32 (ID:1)...\n");
                    // 主动发送也使用 Confirmed
                    LoRa_Service_Send((uint8_t*)line, strlen(line), 0x0001, LORA_OPT_CONFIRMED);
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
    LoRa_Service_Init(&s_LoRaCb, 0x0002); // ESP32 ID = 2

    xTaskCreate(lora_task_entry, "lora_task", 4096, NULL, 5, NULL);
    xTaskCreate(console_task_entry, "console_task", 4096, NULL, 3, NULL);
}
