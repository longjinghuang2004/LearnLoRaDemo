#ifndef __KEY_H
#define __KEY_H

#include <stdint.h>

// --- 1. 核心定义 ---
#define KEY_MAX_COUNT           5
typedef uint32_t KeyMask_t;

typedef enum {
    KEY_EVT_NONE = 0,
    KEY_EVT_DOWN,
    KEY_EVT_UP,
    KEY_EVT_CLICK,
    KEY_EVT_DOUBLE_CLICK,
    KEY_EVT_TRIPLE_CLICK,
    KEY_EVT_HOLD_START,
    KEY_EVT_HOLDING,
    KEY_EVT_HOLD_END,
    KEY_EVT_MODIFIER_CLICK
} KeyEventType_t;

typedef struct {
    KeyMask_t       Mask;
    KeyEventType_t  Type;
    uint32_t        Timestamp;
    uint32_t        Param;
} KeyEvent_t;

// --- 2. 单键对象 (去除了 STM32 特有的 GPIO_TypeDef) ---
typedef struct {
    uint32_t        Pin;        // ESP32 GPIO 编号
    uint8_t         ActiveLevel;// 有效电平 (0:低电平有效, 1:高电平有效)
    uint8_t         ID;
    
    uint8_t         DebounceState;
    uint32_t        DebounceTick;
    uint8_t         IsPressed;
} Key_t;

// --- 3. 接口 ---
// 初始化 (参数变了，去掉了 GPIOx)
void Key_Init(Key_t *key, uint8_t id, uint32_t pin, uint8_t active_level);
void Key_Update(Key_t *key);
uint8_t Key_GetState(Key_t *key);

#define KEY_MASK(id)    ((uint32_t)1 << (id))

// --- 4. HAL 适配接口 (新增) ---
// 这些函数在 key_port_esp32.c 中实现
void Key_HAL_Init_Pin(uint32_t pin, uint8_t active_level);
uint8_t Key_HAL_Read_Pin(uint32_t pin);
uint32_t Key_HAL_GetTick(void);

#endif
