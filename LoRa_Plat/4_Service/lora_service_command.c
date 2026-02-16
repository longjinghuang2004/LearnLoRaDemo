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

#include <inttypes.h> // 为了更好的跨平台兼容bind指令

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

bool LoRa_Service_Command_Process(char *cmd_str, char *out_resp, uint16_t max_len) {
    const LoRa_Config_t *cfg = LoRa_Service_Config_Get();
    
    // 1. 格式检查: CMD
    char *prefix = strtok(cmd_str, ":"); 
    if (prefix == NULL || strcmp(prefix, "CMD") != 0) return false;
    
    // 2. Token 校验
    char *token_str = strtok(NULL, ":");
    if (token_str == NULL) return false;
    
    uint32_t input_token = (uint32_t)strtoul(token_str, NULL, 16);
    if (input_token != cfg->token) {
        LORA_LOG("[SEC] Token Mismatch!\r\n");
        return false; 
    }

    // 3. 提取指令与参数
    char *cmd = strtok(NULL, "="); 
    char *params = strtok(NULL, ""); // 获取剩余所有字符串

    if (cmd == NULL) return false;

    LORA_LOG("[SVC] Executing: %s\r\n", cmd);
    
    // --- INFO 指令 ---
    if (strcmp(cmd, "INFO") == 0) {
        // [修改] 使用辅助函数 _GetRateStr 和 _GetPowerStr 消除编译警告，并提供更友好的信息
        snprintf(out_resp, max_len, "ID:%d,CH:%d,RATE:%s,PWR:%s", 
                 cfg->net_id, 
                 cfg->channel, 
                 _GetRateStr(cfg->air_rate), 
                 _GetPowerStr(cfg->power));
        return true;
    }
    
    // --- CFG 指令 (核心 OTA 逻辑) ---
    // 格式: CMD:TOKEN:CFG=CH:23,PWR:1,RATE:5
    else if (strcmp(cmd, "CFG") == 0 && params != NULL) {
        LoRa_Config_t new_cfg = *cfg;
        bool changed = false;
        
        // 使用逗号分隔多个参数
        char *pair = strtok(params, ",");
        while (pair != NULL) {
            // 解析 Key:Value
            char *key = pair;
            char *val_str = strchr(pair, ':');
            if (val_str) {
                *val_str = '\0'; // 切断 Key
                val_str++;       // 指向 Value
                int val = atoi(val_str);
                
                if (strcmp(key, "CH") == 0) {
                    new_cfg.channel = (uint8_t)val;
                    changed = true;
                } else if (strcmp(key, "PWR") == 0) {
                    new_cfg.power = (uint8_t)val;
                    changed = true;
                } else if (strcmp(key, "RATE") == 0) {
                    new_cfg.air_rate = (uint8_t)val;
                    changed = true;
                } else if (strcmp(key, "NET") == 0) {
                    new_cfg.net_id = (uint16_t)val;
                    changed = true;
                } else if (strcmp(key, "GRP") == 0) {
                    new_cfg.group_id = (uint16_t)val;
                    changed = true;
                } else if (strcmp(key, "ADDR") == 0) {
                    new_cfg.hw_addr = (uint16_t)val;
                    changed = true;
                }
            }
            pair = strtok(NULL, ",");
        }
        
        if (changed) {
            // 1. 更新内存副本
            LoRa_Service_Config_Set(&new_cfg);
            
            // 2. [新增] 通知 Service 层保存到 Flash
            LoRa_Service_NotifyEvent(LORA_EVENT_CONFIG_COMMIT, (void*)&new_cfg);
            
            // 3. 通知 Service 层准备重启
            LoRa_Service_NotifyEvent(LORA_EVENT_REBOOT_REQ, NULL);
            
            snprintf(out_resp, max_len, "OK, Re-init in %dms", LORA_REBOOT_DELAY_MS);
            return true;
        } else {
            snprintf(out_resp, max_len, "ERR: No Change");
            return true;
        }
    }
    
    // --- RST 指令 ---
    else if (strcmp(cmd, "RST") == 0) {
        LoRa_Service_NotifyEvent(LORA_EVENT_REBOOT_REQ, NULL);
        snprintf(out_resp, max_len, "OK, Rebooting...");
        return true;
    }
    
    return false;
}
