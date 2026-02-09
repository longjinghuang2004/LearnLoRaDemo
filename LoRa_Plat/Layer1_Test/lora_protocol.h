#ifndef __LORA_PROTOCOL_H
#define __LORA_PROTOCOL_H

#include "stm32f10x.h"

// --- 新协议定义 (文本模式) ---
// 包头: "CM"
#define LORA_CMD_HEADER_0       'C'
#define LORA_CMD_HEADER_1       'M'

// 包尾: "\n\n" (连续两个换行符)
#define LORA_CMD_TAIL_0         '\n'
#define LORA_CMD_TAIL_1         '\n'

// 指令定义
#define CMD_STR_LED_ON          "LED_on"
#define CMD_STR_LED_OFF         "LED_off"





#endif // __LORA_PROTOCOL_H
