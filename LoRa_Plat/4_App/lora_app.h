#ifndef __LORA_APP_H
#define __LORA_APP_H

#include "LoRaPlatConfig.h" 

// 目标 ID (业务相关)
#define HOST_TARGET_ID      0x0001 // 默认从机ID

// [修改] 初始化函数增加参数
// override_local_id: 如果不为0，则强制使用此ID，忽略Flash中的配置
void LoRa_App_Init(uint16_t override_local_id);

void LoRa_App_Task(void);

// [新增] 暴露全局配置对象供 main.c 访问
extern LoRa_Config_t g_LoRaConfig_Current;

#endif
