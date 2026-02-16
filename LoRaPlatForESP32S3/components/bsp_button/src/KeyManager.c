/* Hardware/Key/KeyManager.c */
#include "KeyManager.h"
#include "Config.h"
//#include "SystemSupport.h"
#include <string.h>
#include "Key.h"  // <--- 确保包含这个，以便使用 Key_HAL_GetTick

// --- 状态机定义 ---
typedef enum {
    KM_STATE_IDLE = 0,      // 空闲
    KM_STATE_COMBO_WAIT,    // 组合窗口 (等待多键按下)
    KM_STATE_PRESSING,      // 按下确认 (等待松开或长按)
    KM_STATE_MULTI_WAIT,    // [新增] 连击等待 (松开后等待再次按下)
    KM_STATE_HOLDING        // 长按中
} KM_State_t;

// --- 内部变量 ---
static Key_t*   s_RegisteredKeys[KM_MAX_KEYS];
static uint8_t  s_KeyCount = 0;

static KM_State_t s_State = KM_STATE_IDLE;
static uint32_t   s_StateTick = 0;          // 状态进入时间
static KeyMask_t  s_ActiveMask = 0;         // 当前激活的按键组合
static KeyMask_t  s_LastHoldMask = 0;       // 长按主体掩码

// [新增] 连击计数器
static uint8_t    s_ClickCount = 0;
static uint32_t   s_MultiWaitTick = 0;      // 连击等待计时

// 事件 FIFO
static KeyEvent_t s_EventBuf;
static uint8_t    s_EventReady = 0;

// --- 内部函数 ---

static void _PushEvent(KeyMask_t mask, KeyEventType_t type, uint32_t param) {
    s_EventBuf.Mask = mask;
    s_EventBuf.Type = type;
    s_EventBuf.Timestamp = Key_HAL_GetTick();
    s_EventBuf.Param = param;
    s_EventReady = 1;
}

static KeyMask_t _ScanCurrentMask(void) {
    KeyMask_t mask = 0;
    for (int i = 0; i < s_KeyCount; i++) {
        Key_Update(s_RegisteredKeys[i]);
        if (Key_GetState(s_RegisteredKeys[i])) {
            mask |= KEY_MASK(s_RegisteredKeys[i]->ID);
        }
    }
    return mask;
}

// --- 接口实现 ---

void KeyManager_Init(void) {
    s_KeyCount = 0;
    s_State = KM_STATE_IDLE;
    s_EventReady = 0;
    s_ClickCount = 0;
    memset(s_RegisteredKeys, 0, sizeof(s_RegisteredKeys));
}

uint8_t KeyManager_Register(Key_t *key) {
    if (s_KeyCount >= KM_MAX_KEYS) return 1;
    s_RegisteredKeys[s_KeyCount++] = key;
    return 0;
}

uint8_t KeyManager_GetEvent(KeyEvent_t *evt) {
    if (s_EventReady) {
        *evt = s_EventBuf;
        s_EventReady = 0;
        return 1;
    }
    return 0;
}

// --- 核心状态机 (V2.0 支持连击) ---
void KeyManager_Tick(void) {
    uint32_t now = Key_HAL_GetTick(); 
    KeyMask_t curr_mask = _ScanCurrentMask();

    switch (s_State) {
        // ---------------- [空闲态] ----------------
        case KM_STATE_IDLE:
            if (curr_mask != 0) {
                s_ActiveMask = curr_mask;
                s_StateTick = now;
                s_ClickCount = 0; // 新的开始，清零连击
                s_State = KM_STATE_COMBO_WAIT;
            }
            break;

        // ---------------- [组合窗口期] ----------------
        case KM_STATE_COMBO_WAIT:
            s_ActiveMask |= curr_mask; // 累积掩码
            if ((now - s_StateTick) > KEY_COMBO_WINDOW_MS) {
                if (s_ActiveMask != 0) {
                    // 窗口结束，确认按下
                    // _PushEvent(s_ActiveMask, KEY_EVT_DOWN, 0); // 可选
                    s_StateTick = now;
                    s_State = KM_STATE_PRESSING;
                } else {
                    s_State = KM_STATE_IDLE;
                }
            }
            break;

        // ---------------- [按下确认态] ----------------
        case KM_STATE_PRESSING:
            // 1. 检查是否松开
            if (curr_mask == 0) {
                // 松开后，不立即报 Click，而是进入连击等待
                s_ClickCount++;
                _PushEvent(s_ActiveMask, KEY_EVT_UP, 0);
                
                s_MultiWaitTick = now;
                s_State = KM_STATE_MULTI_WAIT;
            }
            // 2. 检查长按
            else if ((now - s_StateTick) > KEY_HOLD_TIME_MS) {
                // 只有当按键组合保持完整时才触发长按
                if ((curr_mask & s_ActiveMask) == s_ActiveMask) {
                    _PushEvent(s_ActiveMask, KEY_EVT_HOLD_START, 0);
                    s_LastHoldMask = s_ActiveMask;
                    s_StateTick = now; // 重置计时用于计算持续时间
                    s_ClickCount = 0;  // 长按会打断连击
                    s_State = KM_STATE_HOLDING;
                }
            }
            break;

        // ---------------- [连击等待态] ----------------
        case KM_STATE_MULTI_WAIT:
            // 1. 超时：结算点击次数
            if ((now - s_MultiWaitTick) > KEY_MULTI_CLICK_GAP_MS) {
                if (s_ClickCount == 1) {
                    _PushEvent(s_ActiveMask, KEY_EVT_CLICK, 0);
                } else if (s_ClickCount == 2) {
                    _PushEvent(s_ActiveMask, KEY_EVT_DOUBLE_CLICK, 0);
                } else if (s_ClickCount >= 3) {
                    _PushEvent(s_ActiveMask, KEY_EVT_TRIPLE_CLICK, 0);
                }
                s_ClickCount = 0;
                s_State = KM_STATE_IDLE;
            }
            // 2. 再次按下
            else if (curr_mask != 0) {
                // 必须是相同的按键组合才算连击
                if (curr_mask == s_ActiveMask) {
                    s_StateTick = now; // 重置长按计时
                    s_State = KM_STATE_PRESSING;
                } else {
                    // 按了别的键，强制结算上一次，并开启新流程
                    _PushEvent(s_ActiveMask, KEY_EVT_CLICK, 0); // 降级为单击
                    s_ClickCount = 0;
                    
                    s_ActiveMask = curr_mask;
                    s_StateTick = now;
                    s_State = KM_STATE_COMBO_WAIT;
                }
            }
            break;

        // ---------------- [长按保持态] ----------------
        case KM_STATE_HOLDING:
            // 1. 松开
            if ((curr_mask & s_ActiveMask) != s_ActiveMask) {
                // 计算长按持续时间
                uint32_t duration = now - s_StateTick + KEY_HOLD_TIME_MS;
                _PushEvent(s_ActiveMask, KEY_EVT_HOLD_END, duration);
                _PushEvent(s_ActiveMask, KEY_EVT_UP, 0);
                s_State = KM_STATE_IDLE;
            }
            else {
                // 2. 修饰键逻辑 (Hold A + Click B)
                KeyMask_t modifier = curr_mask ^ s_ActiveMask;
                if (modifier != 0) {
                    static uint32_t s_ModFilterTick = 0;
                    if (now - s_ModFilterTick > 300) {
                        _PushEvent(s_ActiveMask | modifier, KEY_EVT_MODIFIER_CLICK, 0);
                        s_ModFilterTick = now;
                    }
                }
                // 3. 连发逻辑
                if (KEY_REPEAT_RATE_MS > 0) {
                    static uint32_t s_RepeatTick = 0;
                    if (now - s_RepeatTick > KEY_REPEAT_RATE_MS) {
                        uint32_t duration = now - s_StateTick + KEY_HOLD_TIME_MS;
                        _PushEvent(s_ActiveMask, KEY_EVT_HOLDING, duration);
                        s_RepeatTick = now;
                    }
                }
            }
            break;
    }
}
