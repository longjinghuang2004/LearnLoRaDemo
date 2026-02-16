#ifndef __ALGORITHM_H
#define __ALGORITHM_H

#include "InputParser.h"
#include "Model.h"

/**
 * @brief 存储单个时间点上所有计算出的风险指数
 */
typedef struct {
    MinMaxFloat overflow;
    MinMaxFloat instability;
    MinMaxFloat breach;
    MinMaxFloat total;
} RiskResult;

/**
  * @brief  根据输入的预报数据和模型参数，计算所有时间点的风险指数
  * @param  input 指向已解析的输入数据 (ForecastInput)
  * @param  params 指向当前有效的模型参数 (RiskModelParameters)
  * @param  results_array 指向一个RiskResult数组，用于存储计算结果。其大小应至少为 input->time_series_count。
  * @retval 无
  */
void Algorithm_CalculateAllRisks(const ForecastInput* input, const RiskModelParameters* params, RiskResult* results_array);

#endif // __ALGORITHM_H
