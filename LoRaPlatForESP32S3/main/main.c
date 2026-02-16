#include <stdio.h>
#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "nvs_flash.h"

// 引入组件头文件
#include "bsp_led.h"
#include "KeyManager.h"
#include "Key.h"

// 引入 LoRaPlat
#include "lora_service.h"
#include "lora_port.h"

static const char *TAG = "MAIN";

// --- 引脚定义 ---
#define PIN_WS2812      48
#define PIN_BUTTON      21

// --- 声明外部初始化函数 ---
extern void LoRa_OSAL_Init_ESP32(void);

// ============================================================
//                    核心业务逻辑
// ============================================================

/**
 * @brief 接收数据回调
 * @param src_id 发送方的 ID (STM32 的 ID)
 * @param data   接收到的数据 (Payload)
 * @param len    数据长度
 * @param meta   信号强度等元数据
 */
static void App_OnRecvData(uint16_t src_id, const uint8_t *data, uint16_t len, LoRa_RxMeta_t *meta) {
    // 1. 本地 Log 展示 (为了安全，限制打印长度)
    // 注意：data 未必以 \0 结尾，所以使用 %.*s 格式化
    ESP_LOGI(TAG, "[RX] From 0x%04X (RSSI:%d): %.*s", src_id, meta->rssi, len, data);

    // 2. 解析指令 (简单的字符串匹配)
    // 为了防止越界，我们比较时限制长度，或者先拷贝到临时 buffer
    char cmd_buf[32];
    if (len < sizeof(cmd_buf)) {
        memcpy(cmd_buf, data, len);
        cmd_buf[len] = '\0'; // 确保字符串结尾
    } else {
        // 指令太长，截断处理
        memcpy(cmd_buf, data, sizeof(cmd_buf) - 1);
        cmd_buf[sizeof(cmd_buf) - 1] = '\0';
    }

    // --- 执行控制逻辑 ---
    // 提示：这里使用 strncmp 或 strstr 来匹配，忽略可能存在的换行符
    if (strncmp(cmd_buf, "red", 3) == 0) {
        ESP_LOGI(TAG, "Action: LED RED");
        BSP_LED_SetColor(50, 0, 0);
    } 
    else if (strncmp(cmd_buf, "blue", 4) == 0 || strncmp(cmd_buf, "bule", 4) == 0) { 
        // 兼容正确的 blue 和你提到的 bule
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
    else {
        ESP_LOGW(TAG, "Unknown Command: %s", cmd_buf);
    }

    // 3. 实时回传 (Echo)
    // 主机说了什么，原样传回去，证明我收到了
    // 注意：这里直接把接收到的 data 发回给 src_id

    // [新增] 延时避让 ACK
    // 协议栈默认 ACK_DELAY 是 100ms，我们延时 150ms 确保 ACK 已发出
    vTaskDelay(pdMS_TO_TICKS(150));

    bool res = LoRa_Service_Send(data, len, src_id);
    
    if (res) {
        ESP_LOGI(TAG, "Echo sent back to 0x%04X", src_id);
    } else {
        ESP_LOGW(TAG, "Echo failed (Busy)");
    }
}

// 事件回调
static void App_OnEvent(LoRa_Event_t event, void *arg) {
    switch(event) {
        case LORA_EVENT_INIT_SUCCESS: 
            ESP_LOGI(TAG, "LoRa Init Success! Waiting for commands..."); 
            // 启动时闪一下绿灯
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
        default: break;
    }
}

static const LoRa_Callback_t s_LoRaCb = {
    .SaveConfig = NULL,
    .LoadConfig = NULL,
    .GetRandomSeed = NULL,
    .SystemReset = NULL,
    .OnRecvData = App_OnRecvData, // 注册上面的接收函数
    .OnEvent = App_OnEvent
};

// --- LoRa 任务 ---
void lora_task_entry(void *arg) {
    ESP_LOGI(TAG, "LoRa Task Started");
    while (1) {
        LoRa_Service_Run();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

// --- 主入口 ---
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
    
    // 初始化服务 (ID=2, 假设 ESP32 是从机/受控端)
    // STM32 那边如果是 ID=1，这里就设为 2
    LoRa_Service_Init(&s_LoRaCb, 0x0002); 

    // 4. 创建 LoRa 任务
    xTaskCreate(lora_task_entry, "lora_task", 4096, NULL, 5, NULL);

    ESP_LOGI(TAG, "System Ready. Send 'red', 'blue', 'white', 'off' via LoRa.");
}


