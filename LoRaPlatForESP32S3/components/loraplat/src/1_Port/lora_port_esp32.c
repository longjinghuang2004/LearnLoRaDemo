#include "lora_port.h"
#include "lora_osal.h"
#include "driver/uart.h"
#include "driver/gpio.h"
#include "esp_log.h"

#include "esp_random.h" // [新增] 必须包含此头文件才能使用 esp_random()

#include <string.h>

// -----------------------------------------------------------------------------
// 引脚定义 (Pin Config)
// -----------------------------------------------------------------------------
#define LORA_UART_PORT_NUM      UART_NUM_1
#define LORA_PIN_TX             17  // ESP32 TX -> LoRa RXD
#define LORA_PIN_RX             18  // ESP32 RX -> LoRa TXD
#define LORA_PIN_MD0            16  // Output
#define LORA_PIN_AUX            15  // Input

// 驱动层缓冲区大小 (ESP32 内部维护)
#define UART_RX_BUF_SIZE        1024
#define UART_TX_BUF_SIZE        1024

static const char *TAG = "LoRa_Port";

// -----------------------------------------------------------------------------
// 1. 初始化与配置
// -----------------------------------------------------------------------------

void LoRa_Port_Init(uint32_t baudrate)
{
    // 1. 配置 UART 参数
    uart_config_t uart_config = {
        .baud_rate = (int)baudrate,
        .data_bits = UART_DATA_8_BITS,
        .parity    = UART_PARITY_DISABLE,
        .stop_bits = UART_STOP_BITS_1,
        .flow_ctrl = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };

    // 2. 安装驱动 (带 RX/TX 缓冲区，无事件队列)
    ESP_ERROR_CHECK(uart_driver_install(LORA_UART_PORT_NUM, UART_RX_BUF_SIZE, UART_TX_BUF_SIZE, 0, NULL, 0));
    ESP_ERROR_CHECK(uart_param_config(LORA_UART_PORT_NUM, &uart_config));
    ESP_ERROR_CHECK(uart_set_pin(LORA_UART_PORT_NUM, LORA_PIN_TX, LORA_PIN_RX, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE));

    // 3. 配置 GPIO (MD0, AUX)
    gpio_config_t io_conf = {};

    // MD0: 输出
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_OUTPUT;
    io_conf.pin_bit_mask = (1ULL << LORA_PIN_MD0);
    io_conf.pull_down_en = 0;
    io_conf.pull_up_en = 0;
    gpio_config(&io_conf);

    // AUX: 输入 (上拉，防止悬空)
    io_conf.intr_type = GPIO_INTR_DISABLE;
    io_conf.mode = GPIO_MODE_INPUT;
    io_conf.pin_bit_mask = (1ULL << LORA_PIN_AUX);
    io_conf.pull_up_en = 1; 
    gpio_config(&io_conf);

    // 初始状态
    LoRa_Port_SetMD0(false); // 通信模式
    
    ESP_LOGI(TAG, "Init Done. Baud:%d", (int)baudrate);
}

void LoRa_Port_ReInitUart(uint32_t baudrate) {
    // ESP32 可以动态修改波特率，无需重新安装驱动
    uart_set_baudrate(LORA_UART_PORT_NUM, (int)baudrate);
    ESP_LOGI(TAG, "ReInit Baud:%d", (int)baudrate);
}

// -----------------------------------------------------------------------------
// 2. 引脚控制
// -----------------------------------------------------------------------------

void LoRa_Port_SetMD0(bool level) {
    gpio_set_level((gpio_num_t)LORA_PIN_MD0, level ? 1 : 0);
}

void LoRa_Port_SetRST(bool level) {
    // 模块无 RST 引脚，留空
    (void)level;
}

bool LoRa_Port_GetAUX(void) {
    return gpio_get_level((gpio_num_t)LORA_PIN_AUX) == 1;
}

void LoRa_Port_SyncAuxState(void) {
    // ESP32 驱动自动管理，无需手动同步硬件状态
    // 可以在此清空 UART FIFO 以确保干净
    uart_flush_input(LORA_UART_PORT_NUM);
}

// -----------------------------------------------------------------------------
// 3. 发送接口 (TX)
// -----------------------------------------------------------------------------

bool LoRa_Port_IsTxBusy(void) {
    // ESP32 使用软件 RingBuffer，几乎永远不会“忙”（除非缓冲区满）。
    // 为了保持与 STM32 逻辑一致，我们这里只返回 false，
    // 让上层 Manager 认为物理层总是准备好接收数据的。
    // 实际的流控由 ESP32 驱动层处理。
    return false; 
}

uint16_t LoRa_Port_TransmitData(const uint8_t *data, uint16_t len) {
    if (len == 0) return 0;
    
    // 写入 ESP32 的 TX RingBuffer，驱动会自动通过中断/DMA 发送
    int txBytes = uart_write_bytes(LORA_UART_PORT_NUM, (const char*)data, len);
    
    if (txBytes > 0) {
        return (uint16_t)txBytes;
    }
    return 0;
}

// -----------------------------------------------------------------------------
// 4. 接收接口 (RX)
// -----------------------------------------------------------------------------

uint16_t LoRa_Port_ReceiveData(uint8_t *buf, uint16_t max_len) {
    // 非阻塞读取：查看当前缓冲区有多少数据
    size_t available = 0;
    uart_get_buffered_data_len(LORA_UART_PORT_NUM, &available);
    
    if (available == 0) return 0;
    
    // 限制读取长度
    if (available > max_len) available = max_len;
    
    // 从 RingBuffer 读取
    int rxBytes = uart_read_bytes(LORA_UART_PORT_NUM, buf, available, 0); // timeout=0 (非阻塞)
    
    if (rxBytes > 0) {
        return (uint16_t)rxBytes;
    }
    return 0;
}

void LoRa_Port_ClearRxBuffer(void) {
    uart_flush_input(LORA_UART_PORT_NUM);
}

// -----------------------------------------------------------------------------
// 5. 其他能力
// -----------------------------------------------------------------------------

uint32_t LoRa_Port_GetEntropy32(void) {
    // 使用 ESP32 硬件随机数发生器
    return esp_random();
}
