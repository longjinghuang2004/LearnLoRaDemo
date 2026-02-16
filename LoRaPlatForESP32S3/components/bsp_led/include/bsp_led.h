#pragma once
#include <stdint.h>

void BSP_LED_Init(int gpio_num);
void BSP_LED_SetColor(uint8_t r, uint8_t g, uint8_t b);
void BSP_LED_Toggle(void);
