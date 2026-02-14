/**
  ******************************************************************************
  * @file    lora_service_command.c
  * @author  LoRaPlat Team
  * @brief   LoRa 平台指令解析实现 (V3.7 增强版)
  *          支持 INFO 查询、BIND 绑定、FACTORY 恢复出厂
  ******************************************************************************
  */

#include "lora_service_command.h"
#include "lora_service_config.h"
#include "lora_service.h" // 获取事件通知接口
#include "lora_osal.h"
#include <string.h>
#include <stdio.h>

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
    char *cmd = strtok(cmd_str, "="); 
    char *params = strtok(NULL, "");  

    if (cmd == NULL) return false;

    LORA_LOG("[SVC] Cmd: %s\r\n", cmd);
    
    // 获取当前配置副本
    // 注意：这里获取的是 const 指针，我们需要拷贝一份来修改，或者直接修改 RAM 副本
    // LoRa_Service_Config_Get 返回的是 const，为了修改，我们需要拷贝一份
    LoRa_Config_t cfg = *LoRa_Service_Config_Get();
    bool cfg_changed = false;

    // 1. INFO 指令 (查询当前配置)
    if (strcmp(cmd, "INFO") == 0) {
        LORA_LOG("=== LoRa Configuration ===\r\n");
        LORA_LOG("  UUID:    %08X\r\n", cfg.uuid);
        LORA_LOG("  NetID:   %d (0x%04X)\r\n", cfg.net_id, cfg.net_id);
        LORA_LOG("  Group:   %d (0x%04X)\r\n", cfg.group_id, cfg.group_id);
        LORA_LOG("  HW Addr: %04X\r\n", cfg.hw_addr);
        LORA_LOG("  Channel: %d\r\n", cfg.channel);
        LORA_LOG("  Power:   %d (%s)\r\n", cfg.power, _GetPowerStr(cfg.power));
        LORA_LOG("  Rate:    %d (%s)\r\n", cfg.air_rate, _GetRateStr(cfg.air_rate));
        LORA_LOG("  Mode:    %d (%s)\r\n", cfg.tmode, (cfg.tmode==0)?"Trans":"Fixed");
        LORA_LOG("  Token:   %08X\r\n", cfg.token);
        LORA_LOG("==========================\r\n");
        return true;
    }
    
    // 2. BIND 指令 (绑定 ID)
    // 格式: CMD:BIND=UUID,NetID (支持 Hex UUID)
    else if (strcmp(cmd, "BIND") == 0 && params != NULL) {
        uint32_t target_uuid;
        uint16_t new_net_id;
        
        // 尝试解析 Hex 格式 (例如 AAAA1111,5)
        int parsed = sscanf(params, "%x,%hu", &target_uuid, &new_net_id);
        if (parsed != 2) {
             // 尝试解析 Decimal 格式
             parsed = sscanf(params, "%u,%hu", &target_uuid, &new_net_id);
        }

        if (parsed == 2) {
            LORA_LOG("[CMD] Bind Req: UUID=%08X, ID=%d. My UUID=%08X\r\n", target_uuid, new_net_id, cfg.uuid);
            
            if (target_uuid == cfg.uuid) {
                cfg.net_id = new_net_id;
                cfg_changed = true;
                // 触发绑定成功事件
                LoRa_Service_NotifyEvent(LORA_EVENT_BIND_SUCCESS, &new_net_id);
            } else {
                LORA_LOG("[CMD] UUID Mismatch!\r\n");
            }
        } else {
            LORA_LOG("[CMD] Param Parse Error\r\n");
        }
    }
    
    // 3. GROUP 指令 (设置组 ID)
    // 格式: CMD:GROUP=GroupID
    else if (strcmp(cmd, "GROUP") == 0 && params != NULL) {
        uint16_t new_group_id;
        if (sscanf(params, "%hu", &new_group_id) == 1) {
            cfg.group_id = new_group_id;
            cfg_changed = true;
            LoRa_Service_NotifyEvent(LORA_EVENT_GROUP_UPDATE, &new_group_id);
        }
    }
    
    // 4. RST 指令 (重启)
    else if (strcmp(cmd, "RST") == 0) {
        LoRa_Service_NotifyEvent(LORA_EVENT_REBOOT_REQ, NULL);
        // 注意：重启通常由 Adapter 层处理，这里只是通知
        // 如果 Adapter 没处理，这里返回 true 也没用
        // 建议在 main.c 的 OnEvent 中处理 REBOOT_REQ
        return true;
    }
    
    // 5. FACTORY 指令 (恢复出厂)
    else if (strcmp(cmd, "FACTORY") == 0) {
        LoRa_Service_FactoryReset();
        // FactoryReset 内部已经调用了 SaveConfig (如果实现了的话)
        // 并且触发了 FACTORY_RESET 事件
        return true;
    }
    
    // 如果配置发生了变更，保存并应用
    if (cfg_changed) {
        LoRa_Service_Config_Set(&cfg);
        // Config_Set 内部会触发 SaveConfig 回调
        LoRa_Service_NotifyEvent(LORA_EVENT_CONFIG_COMMIT, NULL);
        return true;
    }
    
    return false;
}
