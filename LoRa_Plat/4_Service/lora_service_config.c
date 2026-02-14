/**
  ******************************************************************************
  * @file    lora_service_config.c
  * @author  LoRaPlat Team
  * @brief   LoRa 配置管理实现
  ******************************************************************************
  */

#include "lora_service_config.h"
#include "lora_service.h" // 获取回调接口
#include "lora_osal.h"
#include <string.h>
#include <stdlib.h>

// 内部静态配置副本
static LoRa_Config_t s_CurrentConfig;

// ============================================================
//                    1. 内部辅助
// ============================================================

static void _LoadDefaults(void) {
    memset(&s_CurrentConfig, 0, sizeof(LoRa_Config_t));
    
    s_CurrentConfig.magic    = LORA_CFG_MAGIC;
    s_CurrentConfig.token    = DEFAULT_LORA_TOKEN;
    s_CurrentConfig.net_id   = LORA_ID_UNASSIGNED;
    s_CurrentConfig.group_id = LORA_GROUP_ID_DEFAULT; 
    s_CurrentConfig.hw_addr  = LORA_HW_ADDR_DEFAULT;
    s_CurrentConfig.channel  = DEFAULT_LORA_CHANNEL;
    s_CurrentConfig.power    = (uint8_t)DEFAULT_LORA_POWER;
    s_CurrentConfig.air_rate = (uint8_t)DEFAULT_LORA_RATE;
    s_CurrentConfig.tmode    = (uint8_t)DEFAULT_LORA_TMODE;
    
    // 生成随机 UUID
    //uint32_t seed = 0;
    // 这里需要回调获取种子，但 Config 模块不直接依赖回调
    // 我们假设 Service 层初始化时已经设置了回调
    // 或者通过 OSAL 获取？
    // 暂时策略：使用 OSAL_GetTick 作为简单种子，或者由 Service 层传入
    // 为了解耦，我们假设 Service 层会在 Init 后再次调用 SetConfig 来修正 UUID
    s_CurrentConfig.uuid = 0; 
}

// ============================================================
//                    2. 核心接口实现
// ============================================================

void LoRa_Service_Config_Init(void) {
    _LoadDefaults();
    
#if (defined(LORA_ENABLE_FLASH_SAVE) && LORA_ENABLE_FLASH_SAVE == 1)
    // 尝试从 Flash 加载
    // 这里需要回调函数，但 Config 模块不知道回调在哪里
    // 解决方案：Service 层初始化时，将回调传递给 Config 模块？
    // 或者 Config 模块暴露一个 SetCallbacks 接口？
    // 简单起见，我们假设 Service 层负责调用 Load/Save，Config 模块只负责管理内存副本。
    
    // 修正设计：Config 模块应该只管理内存副本。Flash 读写由 Service 层协调。
    // 但为了封装，我们可以在 Service 层把回调传进来。
    
    // 暂时策略：Config 模块不直接调用 Flash 接口，而是提供 Load/Save 辅助函数供 Service 层调用。
    // 这样 Config 模块就纯粹了。
#endif
}

const LoRa_Config_t* LoRa_Service_Config_Get(void) {
    return &s_CurrentConfig;
}

void LoRa_Service_Config_Set(const LoRa_Config_t *cfg) {
    if (cfg) {
        // 【修改】适配新的临界区接口
        uint32_t primask = OSAL_EnterCritical();
        memcpy(&s_CurrentConfig, cfg, sizeof(LoRa_Config_t));
        OSAL_ExitCritical(primask);
    }
}
void LoRa_Service_Config_FactoryReset(void) {
    _LoadDefaults();
    s_CurrentConfig.magic = 0; // 标记为无效，下次上电会重置
}
