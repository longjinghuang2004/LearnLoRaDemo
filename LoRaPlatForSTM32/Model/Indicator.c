#include "Indicator.h"
#include <stdio.h> // for printf

/**
  * @brief  [已修改] 通过串口打印详细的风险报告，包含四种风险指数
  * @note   采用逐行打印的方式，避免单次printf消耗过多堆栈空间
  */
void Indicator_ShowRiskResults(const ForecastInput* input, const RiskResult* results)
{
    printf("\r\n=============== Risk Calculation Report ===============\r\n");
    printf("Station ID: %s\r\n", input->station_id);
    printf("Found %d time point(s) to process.\r\n", input->time_series_count);
    printf("-------------------------------------------------------\r\n");

    for (int i = 0; i < input->time_series_count; i++)
    {
        printf("\r\n[Time Point %d @ %s]\r\n", i, input->time_series[i].timestamp);
        
        // 打印输入数据
        printf("  --- Input Data ---\r\n");
        printf("    - Wave Height (min/max): %.2f / %.2f m\r\n", 
            input->time_series[i].wave_height.min, 
            input->time_series[i].wave_height.max);
        printf("    - Water Level (min/max): %.2f / %.2f m\r\n", 
            input->time_series[i].water_level.min, 
            input->time_series[i].water_level.max);
            
        // [修改] 打印详细的、分项的计算结果
        printf("  --- Calculated Risk Indices ---\r\n");
        printf("    - Overflow Risk   (漫堤): %.3f / %.3f\r\n", 
            results[i].overflow.min, 
            results[i].overflow.max);
        printf("    - Instability Risk(失稳): %.3f / %.3f\r\n", 
            results[i].instability.min, 
            results[i].instability.max);
        printf("    - Breach Risk     (溃堤): %.3f / %.3f\r\n", 
            results[i].breach.min, 
            results[i].breach.max);
        printf("    - Total Risk      (综合): %.3f / %.3f\r\n", 
            results[i].total.min, 
            results[i].total.max);
    }
    printf("\r\n===================== End of Report =====================\r\n");
}
