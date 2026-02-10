#ifndef __MOD_LORA_H
#define __MOD_LORA_H

#include <stdint.h>
#include <stdbool.h>

// --- Core 层职责 ---
// 1. 提供模块的原子操作 (Atomic Operations)
// 2. 不包含复杂的协议组包逻辑
// 3. 不包含阻塞延时

// 初始化 (绑定 Port)
void LoRa_Core_Init(void);

// 模式控制
// mode: 0=通信模式(MD0=0), 1=配置模式(MD0=1)
// 注意: 此函数只切换引脚，不包含延时。时序由上层控制。
void LoRa_Core_SetMode(uint8_t mode);

// 状态检测
// return: true=忙(AUX=1), false=闲(AUX=0)
bool LoRa_Core_IsBusy(void);

// 硬件复位
void LoRa_Core_HardReset(void);

// 数据透传发送
// 直接调用 Port 发送，不加头尾
uint16_t LoRa_Core_SendRaw(const uint8_t *data, uint16_t len);

// 数据透传接收
// 从 Port 读取数据
uint16_t LoRa_Core_ReadRaw(uint8_t *buf, uint16_t max_len);

// 辅助: 简单的 AT 响应检查
// 在 buf 中查找 expect_str，找到返回 true
bool LoRa_Core_CheckResponse(const uint8_t *buf, uint16_t len, const char *expect_str);

#endif
