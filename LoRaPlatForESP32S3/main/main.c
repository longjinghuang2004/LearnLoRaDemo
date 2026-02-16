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
// #include "KeyManager.h" 
// #include "Key.h"

// 引入 LoRaPlat
#include "lora_service.h"
#include "lora_port.h"
#include "lora_service_command.h"

static const char *TAG = "MAIN";

// --- 引脚定义 ---
#define PIN_WS2812      48
#define PIN_BUTTON      0  // Boot键通常是GPIO0

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

// [新增] 系统复位适配
static void App_SystemReset(void) {
    ESP_LOGW(TAG, "System Restarting...");
    esp_restart();
}

// ============================================================
//                    1. LoRa 回调逻辑
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

    // 3. 全量回传 (Echo)
    // 使用 LORA_OPT_CONFIRMED，确保 STM32 知道我们收到了
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
            ESP_LOGI(TAG, "LoRa Stack Ready (Soft Reboot Done)."); 
            BSP_LED_SetColor(0, 20, 0);
            vTaskDelay(pdMS_TO_TICKS(500));
            BSP_LED_SetColor(0, 0, 0);
            break;
            
        case LORA_EVENT_TX_FINISHED:
            ESP_LOGD(TAG, "TX Finished (ACK OK)"); 
            break;
            
        case LORA_EVENT_TX_FAILED:
            ESP_LOGW(TAG, "TX Failed (Timeout)");
            break;
            
        case LORA_EVENT_CONFIG_COMMIT:
            // Service 层已经调用了 SaveConfig，这里只是日志通知
            ESP_LOGI(TAG, "Config Commit Event Triggered.");
            break;
            
        default: break;
    }
}

static const LoRa_Callback_t s_LoRaCb = {
    .SaveConfig = App_SaveConfig,
    .LoadConfig = App_LoadConfig,
    .GetRandomSeed = NULL, // ESP32 Port层已实现硬件随机数，此处可留空
    .SystemReset = App_SystemReset, // [新增] 绑定复位接口
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
    char resp_buf[128]; // [新增] 用于接收 CMD 执行结果

    printf("\n=== ESP32 LoRaPlat Shell ===\n");
    printf("Type 'CMD:00000000:INFO' to check status\n");

    while (1) {
        if (fgets(line, sizeof(line), stdin) != NULL) {
            char *pos = strchr(line, '\n');
            if (pos) *pos = '\0';
            pos = strchr(line, '\r');
            if (pos) *pos = '\0';

            if (strlen(line) > 0) {
                printf("[Shell] Input: %s\n", line);

                // 1. 本地指令处理
                if (strncmp(line, "CMD:", 4) == 0) {
                    // [修改] 适配新的 Command 接口
                    if (LoRa_Service_Command_Process(line, resp_buf, sizeof(resp_buf))) {
                        printf(" -> CMD Result: %s\n", resp_buf);
                    } else {
                        printf(" -> CMD Failed (Auth Error or Format)\n");
                    }
                }
                // 2. 普通数据发送
                else {
                    printf(" -> Sending to STM32 (ID:1)...\n");
                    // [修改] 增加 LORA_OPT_CONFIRMED
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
    // NVS 初始化 (ESP32 必须)
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    BSP_LED_Init(PIN_WS2812);
    
    LoRa_OSAL_Init_ESP32();
    
    // 初始化协议栈 (ESP32 ID = 2)
    // 内部会自动调用 LoadConfig 和 Driver_Init
    LoRa_Service_Init(&s_LoRaCb, 0x0002); 

    xTaskCreate(lora_task_entry, "lora_task", 4096, NULL, 5, NULL);
    xTaskCreate(console_task_entry, "console_task", 4096, NULL, 3, NULL);
}
