#include "Key.h"

// 基础去抖时间 (ms)
#define KEY_DEBOUNCE_TIME   15

void Key_Init(Key_t *key, uint8_t id, uint32_t pin, uint8_t active_level)
{
    key->Pin = pin;
    key->ActiveLevel = active_level;
    key->ID = id;
    
    key->DebounceState = 0;
    key->DebounceTick = 0;
    key->IsPressed = 0;

    // 调用适配层初始化硬件
    Key_HAL_Init_Pin(pin, active_level);
}

void Key_Update(Key_t *key)
{
    // 调用适配层读取电平
    uint8_t raw_level = Key_HAL_Read_Pin(key->Pin);
    uint8_t is_active = (raw_level == key->ActiveLevel);

    // 调用适配层获取时间
    uint32_t now = Key_HAL_GetTick();

    if (is_active != key->DebounceState) {
        key->DebounceState = is_active;
        key->DebounceTick = now;
    } else {
        if ((now - key->DebounceTick) >= KEY_DEBOUNCE_TIME) {
            key->IsPressed = is_active;
        }
    }
}

uint8_t Key_GetState(Key_t *key)
{
    return key->IsPressed;
}
