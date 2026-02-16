#ifndef __MODEL_H
#define __MODEL_H

#include "stm32f10x.h"
#include "Model.h"   // 需要了解 RiskModelParameters 结构
#include "cJSON.h"   // 需要使用 cJSON 库

typedef struct {
    // --- 将1字节的标志位放在最前面 ---
    uint8_t written_flag;
    
    // --- 为了保证后续float成员4字节对齐，我们手动填充3个字节 ---
    uint8_t padding[3]; 
    
    // --- 新增的设备信息 ---
    float station_id;
    float location; // 暂时用float，后续可改为更复杂结构
    
    // --- 原始模型参数 ---
    float overflow_base;
    float instability_base;
    float w_overflow_wave_height;
    float w_overflow_water_level;
    float w_instability_wave_height;
    float w_instability_water_level;
    float w_breach_overflow;
    float w_breach_instability;
    float w_total_overflow;
    float w_total_breach;
    float norm_wave_height_L;
    float norm_wave_height_U;
    float norm_water_level_L;
    float norm_water_level_U;
    float threshold_low;
    float threshold_medium;
    float threshold_high;

} RiskModelParameters;


// 声明一个全局变量，用于在RAM中存储当前有效的模型参数
extern RiskModelParameters g_ModelParams;

// 函数原型
void Model_Init(void);

/**
  * @brief  解析输入的"param"类型JSON字符串，并更新模型参数结构体
  * @param  json_string 指向包含参数JSON数据的字符串
  * @param  params_to_update 指向一个将被更新的 RiskModelParameters 结构体实例
  * @retval int 返回成功更新的参数数量，如果解析失败则返回 0。
  */
int Parse_ParamInput(const char *json_string, RiskModelParameters *params_to_update);

#endif
