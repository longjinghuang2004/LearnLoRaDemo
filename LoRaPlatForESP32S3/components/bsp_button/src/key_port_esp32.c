#include "Key.h"
#include "driver/gpio.h"
#include "esp_timer.h"

// 硬件初始化适配
void Key_HAL_Init_Pin(uint32_t pin, uint8_t active_level)
{
    gpio_reset_pin((gpio_num_t)pin);
    gpio_set_direction((gpio_num_t)pin, GPIO_MODE_INPUT);
    
    if (active_level == 0) {
        gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLUP_ONLY); // 低电平有效 -> 上拉
    } else {
        gpio_set_pull_mode((gpio_num_t)pin, GPIO_PULLDOWN_ONLY); // 高电平有效 -> 下拉
    }
}

// 硬件读取适配
uint8_t Key_HAL_Read_Pin(uint32_t pin)
{
    return (uint8_t)gpio_get_level((gpio_num_t)pin);
}

// 时间获取适配 (ms)
uint32_t Key_HAL_GetTick(void)
{
    // esp_timer_get_time() 返回微秒，除以 1000 转毫秒
    return (uint32_t)(esp_timer_get_time() / 1000);
}
