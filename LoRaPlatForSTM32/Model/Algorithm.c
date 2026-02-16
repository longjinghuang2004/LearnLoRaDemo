#include "Algorithm.h"
#include <string.h> // for memset
#include <math.h>   // for expf (float version of exp)

/**
  * @brief  (内部函数) 将一个值限制在指定的min和max之间
  */
static float clamp(float value, float min_val, float max_val) {
    if (value < min_val) return min_val;
    if (value > max_val) return max_val;
    return value;
}

/**
  * @brief  (内部函数) 使用Sigmoid函数对输入值进行归一化处理
  *         将 value 从 [L, U] 的范围非线性地映射到 [0, 1]
  */
static float sigmoid_normalize(float value, float L, float U) {
    if (U - L == 0) return 0.5f; // 防止除以零，返回中间值
    
    float p = (value - L) / (U - L);
    // 使用 expf 是因为我们的所有计算都是基于 float 的，效率更高
    float result = 1.0f / (1.0f + expf(-10.0f * p + 5.0f));
    
    return result;
}

/**
  * @brief  根据输入的预报数据和模型参数，计算所有时间点的风险指数
  */
void Algorithm_CalculateAllRisks(const ForecastInput* input, const RiskModelParameters* params, RiskResult* results_array)
{
    memset(results_array, 0, sizeof(RiskResult) * input->time_series_count);

    for (int i = 0; i < input->time_series_count; i++)
    {
        const TimeSeriesData* current_data = &input->time_series[i];

        // 1. 数据归一化
        float norm_wh_min = sigmoid_normalize(current_data->wave_height.min, params->norm_wave_height_L, params->norm_wave_height_U);
        float norm_wh_max = sigmoid_normalize(current_data->wave_height.max, params->norm_wave_height_L, params->norm_wave_height_U);
        float norm_wl_min = sigmoid_normalize(current_data->water_level.min, params->norm_water_level_L, params->norm_water_level_U);
        float norm_wl_max = sigmoid_normalize(current_data->water_level.max, params->norm_water_level_L, params->norm_water_level_U);

        // 2. 计算漫顶风险 (Overflow)
        results_array[i].overflow.min = params->overflow_base + params->w_overflow_wave_height * norm_wh_min + params->w_overflow_water_level * norm_wl_min;
        results_array[i].overflow.max = params->overflow_base + params->w_overflow_wave_height * norm_wh_max + params->w_overflow_water_level * norm_wl_max;

        // 3. 计算失稳风险 (Instability)
        results_array[i].instability.min = params->instability_base + params->w_instability_wave_height * norm_wh_min + params->w_instability_water_level * norm_wl_min;
        results_array[i].instability.max = params->instability_base + params->w_instability_wave_height * norm_wh_max + params->w_instability_water_level * norm_wl_max;

        // 4. 计算溃口风险 (Breach)
        results_array[i].breach.min = params->w_breach_overflow * results_array[i].overflow.min + params->w_breach_instability * results_array[i].instability.min;
        results_array[i].breach.max = params->w_breach_overflow * results_array[i].overflow.max + params->w_breach_instability * results_array[i].instability.max;

        // 5. 计算综合风险 (Total)
        results_array[i].total.min = params->w_total_overflow * results_array[i].overflow.min + params->w_total_breach * results_array[i].breach.min;
        results_array[i].total.max = params->w_total_overflow * results_array[i].overflow.max + params->w_total_breach * results_array[i].breach.max;
                                     
        // 6. (推荐) 对所有最终结果进行钳位，确保在[0,1]范围内
        results_array[i].total.min = clamp(results_array[i].total.min, 0.0f, 1.0f);
        results_array[i].total.max = clamp(results_array[i].total.max, 0.0f, 1.0f);
    }
}
