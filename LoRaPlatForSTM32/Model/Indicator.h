#ifndef __INDICATOR_H
#define __INDICATOR_H

#include "InputParser.h"
#include "Algorithm.h"

/**
  * @brief  通过串口打印输入的预报数据和计算出的风险结果
  * @param  input 指向已解析的输入数据
  * @param  results 指向包含风险计算结果的数组
  * @retval 无
  */
void Indicator_ShowRiskResults(const ForecastInput* input, const RiskResult* results);

#endif // __INDICATOR_H
