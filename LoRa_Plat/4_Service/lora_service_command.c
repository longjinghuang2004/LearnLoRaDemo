/**
  ******************************************************************************
  * @file    lora_service_command.c
  * @author  LoRaPlat Team
  * @brief   LoRa 平台指令解析实现
  ******************************************************************************
  */

#include "lora_service_command.h"
#include "lora_service_config.h"
#include "lora_service.h" // 获取事件通知接口
#include "lora_osal.h"
#include <string.h>
#include <stdio.h>

// ============================================================
//                    1. 核心接口实现
// ============================================================

bool LoRa_Service_Command_Process(char *cmd_str) {
    char *cmd = strtok(cmd_str, "="); 
    char *params = strtok(NULL, "");  

    if (cmd == NULL) return false;

    LORA_LOG("[SVC] Cmd: %s\r\n", cmd);
    
    // 获取当前配置副本
    LoRa_Config_t cfg = *LoRa_Service_Config_Get();
    bool cfg_changed = false;

    // 1. BIND 指令
    if (strcmp(cmd, "BIND") == 0 && params != NULL) {
        uint32_t target_uuid;
        uint16_t new_net_id;
        if (sscanf(params, "%u,%hu", &target_uuid, &new_net_id) == 2) {
            if (target_uuid == cfg.uuid) {
                cfg.net_id = new_net_id;
                cfg_changed = true;
                // 通知 Service 层
                // 这里需要回调，但 Command 模块不直接依赖 Service 回调
                // 解决方案：Command 模块只修改 Config，返回 true。
                // Service 层在调用 Process 后，检查 Config 是否变化？
                // 或者 Command 模块调用 Service 提供的 Notify 接口。
                
                // 暂时策略：直接调用 Service 层的 Notify 接口 (需要 Service.h 暴露)
                // LoRa_Service_NotifyEvent(LORA_EVENT_BIND_SUCCESS, &new_net_id);
            }
        }
    }
    // 2. GROUP 指令
    else if (strcmp(cmd, "GROUP") == 0 && params != NULL) {
        uint16_t new_group_id;
        if (sscanf(params, "%hu", &new_group_id) == 1) {
            cfg.group_id = new_group_id;
            cfg_changed = true;
        }
    }
    // 3. RST 指令
    else if (strcmp(cmd, "RST") == 0) {
        // LoRa_Service_NotifyEvent(LORA_EVENT_REBOOT_REQ, NULL);
        return true;
    }
    // 4. FACTORY 指令
    else if (strcmp(cmd, "FACTORY") == 0) {
        LoRa_Service_Config_FactoryReset();
        return true;
    }
    
    if (cfg_changed) {
        LoRa_Service_Config_Set(&cfg);
        return true;
    }
    
    return false;
}
