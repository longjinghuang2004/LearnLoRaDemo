/* Hardware/Key/KeyManager.h */
#ifndef __KEY_MANAGER_H
#define __KEY_MANAGER_H

#include "Key.h"

// --- 配置参数 ---
#define KM_MAX_KEYS             5       // 系统中注册的最大按键数
#define KM_COMBO_WINDOW_MS      50      // 组合键判定窗口 (越小越灵敏，越大越容易触发组合)
#define KM_HOLD_TIME_MS         800     // 长按触发阈值
#define KM_REPEAT_RATE_MS       200     // 长按保持时的连发间隔 (0表示不连发)

// --- 接口 ---

/**
 * @brief  初始化按键管理器
 */
void KeyManager_Init(void);

/**
 * @brief  注册一个按键对象到管理器
 * @param  key: 已初始化的 Key_t 对象指针
 * @retval 0: 成功, 1: 失败(满)
 */
uint8_t KeyManager_Register(Key_t *key);

/**
 * @brief  管理器核心任务 (需周期性调用，建议 5ms-10ms)
 * @note   内部会调用所有注册按键的 Key_Update
 */
void KeyManager_Tick(void);

/**
 * @brief  检查是否有待处理的事件 (轮询模式)
 * @param  evt: 用于接收事件的结构体指针
 * @retval 1: 获取到事件, 0: 无事件
 */
uint8_t KeyManager_GetEvent(KeyEvent_t *evt);

#endif
