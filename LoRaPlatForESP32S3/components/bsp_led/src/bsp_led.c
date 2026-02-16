#include "bsp_led.h"
#include "led_strip.h"
#include "esp_log.h"

static const char *TAG = "LED";
static led_strip_handle_t led_strip;

void BSP_LED_Init(int gpio_num)
{
    ESP_LOGI(TAG, "Init WS2812 on GPIO %d", gpio_num);

    led_strip_config_t strip_config = {
        .strip_gpio_num = gpio_num,
        .max_leds = 1, // 板载只有一个灯
    };
    
    led_strip_rmt_config_t rmt_config = {
        .resolution_hz = 10 * 1000 * 1000, // 10MHz
        .flags.with_dma = false,
    };
    
    ESP_ERROR_CHECK(led_strip_new_rmt_device(&strip_config, &rmt_config, &led_strip));
    
    // 初始关闭
    led_strip_clear(led_strip);
}

void BSP_LED_SetColor(uint8_t r, uint8_t g, uint8_t b)
{
    // 设置第0个灯的颜色
    led_strip_set_pixel(led_strip, 0, r, g, b);
    led_strip_refresh(led_strip);
}

void BSP_LED_Toggle(void)
{
    static bool is_on = false;
    is_on = !is_on;
    if (is_on) {
        BSP_LED_SetColor(20, 0, 0); // 红色 (亮度不要太高，刺眼)
    } else {
        BSP_LED_SetColor(0, 0, 0);  // 灭
    }
}
