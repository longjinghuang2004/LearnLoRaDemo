/**
  ******************************************************************************
  * @file    lora_service_command.c
  * @author  LoRaPlat Team
  * @brief   LoRa 平台指令解析实现 (V3.8 Fix Warnings)
  ******************************************************************************
  */

#include "lora_service_command.h"
#include "lora_service_config.h"
#include "lora_service.h"
#include "lora_osal.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h> // 必须包含，用于 strtoul

// ============================================================
//                    1. 内部辅助函数
// ============================================================

// 辅助：将功率值转换为字符串
static const char* _GetPowerStr(uint8_t power) {
    switch(power) {
        case 0: return "11dBm";
        case 1: return "14dBm";
        case 2: return "17dBm";
        case 3: return "20dBm";
        default: return "Unknown";
    }
}

// 辅助：将速率值转换为字符串
static const char* _GetRateStr(uint8_t rate) {
    switch(rate) {
        case 0: return "0.3k";
        case 1: return "1.2k";
        case 2: return "2.4k";
        case 3: return "4.8k";
        case 4: return "9.6k";
        case 5: return "19.2k";
        default: return "Unknown";
    }
}

// ============================================================
//                    2. 核心接口实现
// ============================================================

bool LoRa_Service_Command_Process(char *cmd_str) {
    // 1. 获取当前配置 (用于比对 Token)
    const LoRa_Config_t *cfg = LoRa_Service_Config_Get();
    
    // 2. 格式检查: CMD
    char *prefix = strtok(cmd_str, ":"); 
    if (prefix == NULL || strcmp(prefix, "CMD") != 0) return false;
    
    // 3. [安全核心] 提取并校验 Token
    char *token_str = strtok(NULL, ":");
    if (token_str == NULL) {
        LORA_LOG("[SEC] Missing Token in Command!\r\n");
        return false;
    }
    
    // 使用 strtoul 解析 hex 字符串
    uint32_t input_token = (uint32_t)strtoul(token_str, NULL, 16);
    if (input_token != cfg->token) {
        LORA_LOG("[SEC] Token Mismatch! Expect %08X, Got %08X\r\n", cfg->token, input_token);
        return false; // 鉴权失败，拒绝执行
    }

    // 4. 提取具体指令与参数
    char *cmd = strtok(NULL, "="); 
    char *params = strtok(NULL, ""); // 获取剩余所有字符串作为参数

    if (cmd == NULL) return false;

    LORA_LOG("[SVC] Auth OK. Executing: %s\r\n", cmd);
    
    // 准备修改配置
    LoRa_Config_t new_cfg = *cfg;
    bool cfg_changed = false;

    // --- 指令处理逻辑 ---

    // INFO
    if (strcmp(cmd, "INFO") == 0) {
        LORA_LOG("=== LoRa Configuration ===\r\n");
        LORA_LOG("  UUID:    %08X\r\n", new_cfg.uuid);
        LORA_LOG("  NetID:   %d\r\n", new_cfg.net_id);
        LORA_LOG("  Group:   %d\r\n", new_cfg.group_id);
        LORA_LOG("  Token:   %08X\r\n", new_cfg.token);
        LORA_LOG("  Chan:    %d\r\n", new_cfg.channel);
        // [修复] 这里调用了辅助函数，消除了 Warning #177
        LORA_LOG("  Power:   %s\r\n", _GetPowerStr(new_cfg.power));
        LORA_LOG("  Rate:    %s\r\n", _GetRateStr(new_cfg.air_rate));
        LORA_LOG("==========================\r\n");
        return true;
    }
    
    // BIND (格式: BIND=UUID,NetID)
    else if (strcmp(cmd, "BIND") == 0 && params != NULL) {
        uint32_t target_uuid;
        uint16_t new_net_id;
        
        // 尝试解析 Hex UUID
        int parsed = sscanf(params, "%x,%hu", &target_uuid, &new_net_id);
        if (parsed != 2) parsed = sscanf(params, "%u,%hu", &target_uuid, &new_net_id);

        if (parsed == 2) {
            if (target_uuid == new_cfg.uuid) {
                new_cfg.net_id = new_net_id;
                cfg_changed = true;
                LoRa_Service_NotifyEvent(LORA_EVENT_BIND_SUCCESS, &new_net_id);
            } else {
                LORA_LOG("[CMD] UUID Mismatch for BIND.\r\n");
            }
        }
    }
    
    // RST
    else if (strcmp(cmd, "RST") == 0) {
        LoRa_Service_NotifyEvent(LORA_EVENT_REBOOT_REQ, NULL);
        return true;
    }
    
    // FACTORY
    else if (strcmp(cmd, "FACTORY") == 0) {
        LoRa_Service_FactoryReset();
        return true;
    }
    
    // 应用变更
    if (cfg_changed) {
        LoRa_Service_Config_Set(&new_cfg);
        LoRa_Service_NotifyEvent(LORA_EVENT_CONFIG_COMMIT, NULL);
        return true;
    }
    
    return false;
}
