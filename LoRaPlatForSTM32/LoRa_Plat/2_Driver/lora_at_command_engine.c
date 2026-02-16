/**
  ******************************************************************************
  * @file    lora_at_command_engine.c
  * @author  LoRaPlat Team
  * @brief   AT 指令执行引擎实现 (阻塞版)
  ******************************************************************************
  */

#include "lora_at_command_engine.h"
#include "lora_port.h"
#include "lora_osal.h"
#include <string.h>
#include <stdio.h>

// 内部接收缓冲区
static char s_AtRxBuf[128];
static uint16_t s_AtRxIdx = 0;

void LoRa_AT_Init(void) {
    s_AtRxIdx = 0;
    LoRa_Port_ClearRxBuffer();
}

AT_Status_t LoRa_AT_Execute(const char *cmd, const char *expect, uint32_t timeout) {
    // 1. 清空接收缓存
    LoRa_Port_ClearRxBuffer();
    s_AtRxIdx = 0;
    memset(s_AtRxBuf, 0, sizeof(s_AtRxBuf));
    
    // 2. 发送指令
    // 注意：这里调用 Port 层的发送接口
    // 如果 Port 忙，我们需要等待吗？是的，因为这是阻塞接口。
    uint32_t start = OSAL_GetTick();
    while (LoRa_Port_IsTxBusy()) {
        if (OSAL_GetTick() - start > 100) return AT_STATUS_ERROR; // 发送超时
    }
    
    LoRa_Port_TransmitData((const uint8_t*)cmd, strlen(cmd));
    
    // 3. 等待响应
    start = OSAL_GetTick();
    
    while (OSAL_GetTick() - start < timeout) {
        uint8_t byte;
        // 逐字节读取
        if (LoRa_Port_ReceiveData(&byte, 1) > 0) {
            s_AtRxBuf[s_AtRxIdx++] = byte;
            if (s_AtRxIdx >= sizeof(s_AtRxBuf) - 1) s_AtRxIdx = sizeof(s_AtRxBuf) - 2; // 防止溢出
            s_AtRxBuf[s_AtRxIdx] = '\0'; // 保持字符串结尾
            
            // 检查是否包含期望的字符串
            if (strstr(s_AtRxBuf, expect)) {
                OSAL_DelayMs(20); // 稍作延时确保后续字符发完
                return AT_STATUS_OK;
            }
        }
    }
    
    return AT_STATUS_TIMEOUT;
}
