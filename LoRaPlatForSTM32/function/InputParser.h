#ifndef __INPUT_PARSER_H
#define __INPUT_PARSER_H

#include "cJSON.h"

// --- 常量定义 ---
#define STATION_ID_MAX_LEN      16
#define BATCH_ID_MAX_LEN        32
#define TIMESTAMP_MAX_LEN       20
// [修改] 根据新的需求（7-24个时间节点），将最大帧数调整为24
#define MAX_FRAMES_PER_BATCH    24

// --- 数据结构定义 ---

/**
 * @brief 存储min/max浮点数值对
 */
typedef struct {
    float min;
    float max;
} MinMaxFloat;

/**
 * @brief 存储单个时间点上的所有预报数据 (即JSON中的payload)
 */
typedef struct {
    char timestamp[TIMESTAMP_MAX_LEN];
    MinMaxFloat wave_height;
    MinMaxFloat water_level;
} TimeSeriesData;

/**
 * @brief [新结构] 存储从单个分包JSON解析出的元数据和负载
 */
typedef struct {
    char batch_id[BATCH_ID_MAX_LEN];
    char station_id[STATION_ID_MAX_LEN];
    int frame_index;
    int total_frames;
    TimeSeriesData payload;
} BatchFrame;


/**
 * @brief [结构用途改变] 现在用于存储一个完整批次的所有数据
 */
typedef struct {
    char station_id[STATION_ID_MAX_LEN];
    TimeSeriesData time_series[MAX_FRAMES_PER_BATCH];
    int time_series_count;
} ForecastInput;


// --- 函数原型声明 ---

/**
  * @brief  [新函数] 解析输入的"data"类型分包JSON字符串
  */
int Parse_BatchFrame(const char *json_string, BatchFrame *output_frame);

#endif // __INPUT_PARSER_H
