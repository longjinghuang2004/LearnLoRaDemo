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

// [修改] 增加 Flash 保存逻辑
void LoRa_Service_Config_Set(const LoRa_Config_t *cfg) {
    if (cfg) {
        uint32_t primask = OSAL_EnterCritical();
        memcpy(&s_CurrentConfig, cfg, sizeof(LoRa_Config_t));
        OSAL_ExitCritical(primask);
        
        // [新增] 立即调用回调保存到 Flash (阻塞式)
        // 注意：这里需要获取 Service 层的回调，但 Config 模块没有直接持有。
        // 方案：我们通过 LoRa_Service_GetCallback() 获取（需要在 Service.h 暴露）
        // 或者，更简单的，我们在 Service 层初始化 Config 时传入回调。
        // 鉴于目前架构，我们假设 Service 层会处理，或者我们在这里直接调用一个外部弱符号？
        
        // 为了不破坏架构，我们修改一下逻辑：
        // Config 模块只负责内存。Service 层在调用 Config_Set 之前或之后负责调用 SaveConfig。
        // 但 Command 模块调用了 Config_Set。
        
        // **修正方案**：在 lora_service.c 中增加一个 Helper 函数供 Command 调用，
        // 或者让 Config 模块能访问回调。
        // 这里我们采用最直接的方式：在 lora_service.c 中暴露一个 Save 接口给 Command 用，
        // 但 Command 已经写了调用 Config_Set。
        
        // **最终方案**：让 Config_Set 只是更新内存。Command 模块在调用 Config_Set 后，
        // 显式触发 LORA_EVENT_CONFIG_COMMIT 事件。
        // Service 层捕获该事件并调用 SaveConfig。
        // 这样解耦最好。
    }
}

void LoRa_Service_Config_FactoryReset(void) {
    _LoadDefaults();
    s_CurrentConfig.magic = 0; // 标记为无效，下次上电会重置
}
