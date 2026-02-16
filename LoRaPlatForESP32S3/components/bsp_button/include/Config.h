#ifndef __CONFIG_H
#define __CONFIG_H

/* ============================================================
 *                 System Settings
 * ============================================================ */

// 系统时钟频率 (STM32F103 默认 72MHz)
#define SYSTEM_CORE_CLOCK       72000000

// 系统节拍频率 (Hz)，1000 表示每秒 1000 次，即 1ms 一个节拍
#define SYSTEM_TICK_FREQ        1000

// 计算每个节拍的微秒数 (1000Hz -> 1000us = 1ms)
#define SYSTEM_TICK_PERIOD_US   (1000000 / SYSTEM_TICK_FREQ)

/* ============================================================
 *                 Encoder Settings
 * ============================================================ */
// 编码器硬件分频系数 (EC11编码器转一格有4个脉冲)
#define ENCODER_HW_DIVIDER      1
// 编码器软件乘数 (旋转一格，数值变化量)
#define ENCODER_SW_MULTIPLIER   15



/* ============================================================
 *                 Key Event Settings (Refactored)
 * ============================================================ */
// --- Key Settings ---
#define KEY_DEBOUNCE_TIME_MS        20      // 消抖时间
#define KEY_COMBO_WINDOW_MS         50      // 组合键判定窗口
#define KEY_HOLD_TIME_MS            800     // 长按触发阈值
#define KEY_REPEAT_RATE_MS          0       // 长按连发间隔 (0=关闭)


// [新增] 连击判定窗口
// 松手后，如果在此时间内再次按下，则判定为连击；否则结算为单击
#define KEY_MULTI_CLICK_GAP_MS      250 

#endif
