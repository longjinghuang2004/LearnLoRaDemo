#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "driver/uart.h" // 用于配置串口缓冲

// 引入组件头文件
#include "bsp_led.h"
#include "KeyManager.h"
#include "Key.h"

// 引入 LoRaPlat
#include "lora_service.h"
#include "lora_port.h"
#include "lora_service_command.h" // [新增] 引入指令处理头文件

static const char *TAG = "MAIN";

// --- 引脚定义 ---
#define PIN_WS2812      48
#define PIN_BUTTON      21

// --- 声明外部初始化函数 ---
extern void LoRa_OSAL_Init_ESP32(void);

// ============================================================
//                    NVS 存储适配 (新增)
// ============================================================

#define NVS_NAMESPACE "lora_store"
#define NVS_KEY_CFG   "sys_cfg"

static void App_SaveConfig(const LoRa_Config_t *cfg) {
    nvs_handle_t my_handle;
    esp_err_t err;

    // 1. 打开 NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Error (%s) opening NVS handle!", esp_err_to_name(err));
        return;
    }

    // 2. 写入结构体 (Blob)
    err = nvs_set_blob(my_handle, NVS_KEY_CFG, cfg, sizeof(LoRa_Config_t));
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to write config!");
    } else {
        // 3. 提交
        err = nvs_commit(my_handle);
        if (err == ESP_OK) {
            ESP_LOGI(TAG, "Config committed to NVS.");
        }
    }

    // 4. 关闭
    nvs_close(my_handle);
}

static void App_LoadConfig(LoRa_Config_t *cfg) {
    nvs_handle_t my_handle;
    esp_err_t err;

    // 1. 打开 NVS
    err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &my_handle);
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "NVS empty or open failed, using defaults.");
        return;
    }

    // 2. 读取结构体
    size_t required_size = sizeof(LoRa_Config_t);
    err = nvs_get_blob(my_handle, NVS_KEY_CFG, cfg, &required_size);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "Config loaded from NVS.");
    } else {
        ESP_LOGW(TAG, "Failed to load config (First run?)");
    }

    // 3. 关闭
    nvs_close(my_handle);
}


// ============================================================
//                    1. LoRa 回调逻辑
// ============================================================

static void App_OnRecvData(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta) {
    ESP_LOGI(TAG, "[RX] From 0x%04X (RSSI:%d): %.*s", src_id, meta->rssi, len, data);

    char cmd_buf[32];
    if (len < sizeof(cmd_buf)) {
        memcpy(cmd_buf, data, len);
        cmd_buf[len] = '\0';
    } else {
        memcpy(cmd_buf, data, sizeof(cmd_buf) - 1);
        cmd_buf[sizeof(cmd_buf) - 1] = '\0';
    }

    // --- LED 控制 ---
    if (strncmp(cmd_buf, "red", 3) == 0) {
        ESP_LOGI(TAG, "Action: LED RED");
        BSP_LED_SetColor(50, 0, 0);
    } 
    else if (strncmp(cmd_buf, "blue", 4) == 0 || strncmp(cmd_buf, "bule", 4) == 0) { 
        ESP_LOGI(TAG, "Action: LED BLUE");
        BSP_LED_SetColor(0, 0, 50);
    } 
    else if (strncmp(cmd_buf, "white", 5) == 0) {
        ESP_LOGI(TAG, "Action: LED WHITE");
        BSP_LED_SetColor(20, 20, 20);
    } 
    else if (strncmp(cmd_buf, "off", 3) == 0) {
        ESP_LOGI(TAG, "Action: LED OFF");
        BSP_LED_SetColor(0, 0, 0);
    }

    // --- Echo 回传 ---
    // 此时 LoRaPlat 已经有了发送队列，直接发送即可，无需手动延时
    bool res = LoRa_Service_Send(data, len, src_id);
    if (!res) {
        ESP_LOGW(TAG, "Echo failed (Queue Full)");
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
            ESP_LOGD(TAG, "TX Finished (ACK OK)");
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
    .SaveConfig = App_SaveConfig,  // <--- 注册保存函数
    .LoadConfig = App_LoadConfig,  // <--- 注册加载函数
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

// --- [新增] PC 串口控制台任务 ---
void console_task_entry(void *arg) {
    char line[128];
    
    // 提示符
    printf("\n");
    printf("========================================\n");
    printf("   ESP32 LoRaPlat Shell\n");
    printf("   Try: CMD:00000000:INFO\n");
    printf("========================================\n");

    while (1) {
        // 从 stdin (USB/UART0) 读取一行数据，阻塞式
        if (fgets(line, sizeof(line), stdin) != NULL) {
            // 去除换行符
            char *pos = strchr(line, '\n');
            if (pos) *pos = '\0';
            pos = strchr(line, '\r');
            if (pos) *pos = '\0';

            if (strlen(line) > 0) {
                printf("[Shell] Input: %s\n", line);

                // 1. 尝试作为平台指令解析
                if (strncmp(line, "CMD:", 4) == 0) {
                    if (LoRa_Service_Command_Process(line)) {
                        printf(" -> Command Executed OK.\n");
                    } else {
                        printf(" -> Command Failed (Auth Error or Format Error).\n");
                    }
                }
                // 2. 也可以作为普通数据发送给 STM32 (ID=1)
                else {
                    printf(" -> Sending to STM32 (ID:1)...\n");
                    LoRa_Service_Send((uint8_t*)line, strlen(line), 0x0001);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(50)); // 让出 CPU
    }
}

// ============================================================
//                    3. 主入口
// ============================================================
void app_main(void)
{
    // 1. 系统初始化
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    // 2. BSP 初始化
    BSP_LED_Init(PIN_WS2812);
    
    // 3. LoRa 初始化
    LoRa_OSAL_Init_ESP32();
    LoRa_Service_Init(&s_LoRaCb, 0x0002); // ESP32 ID = 2

    // 4. 创建任务
    // LoRa 核心任务 (优先级高一点)
    xTaskCreate(lora_task_entry, "lora_task", 4096, NULL, 5, NULL);
    
    // [新增] 控制台任务 (优先级低一点)
    xTaskCreate(console_task_entry, "console_task", 4096, NULL, 3, NULL);
}
