#ifndef __LORA_APP_H
#define __LORA_APP_H

#include "LoRaPlatConfig.h" // 引入配置

// 角色定义 (可以在这里覆盖，或者在 Config.h 里定义)
// 建议：角色属于业务逻辑，留在 App.h
#define LORA_APP_ROLE       1 // 0:Slave, 1:Host

// 目标 ID (业务相关)
#define HOST_TARGET_ID      0x0002
#define APP_SEND_INTERVAL   3000

void LoRa_App_Init(void);
void LoRa_App_Task(void);

#endif
