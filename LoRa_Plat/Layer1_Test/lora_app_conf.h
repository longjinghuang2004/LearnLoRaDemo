#ifndef __LORA_APP_CONF_H
#define __LORA_APP_CONF_H

#include "mod_lora.h"

// 暴露全局对象供业务逻辑使用
extern LoRa_Dev_t g_LoRa;

// 初始化函数
void LoRa_App_Init(void);

#endif
