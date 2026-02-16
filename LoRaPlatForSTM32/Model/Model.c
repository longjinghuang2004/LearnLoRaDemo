// Model/Model.c

#include "Model.h"
#include "Flash.h"
#include "Serial.h"
#include "Delay.h" // 引入Delay.h

#include <stdio.h>
#include <stdlib.h> // 用于 atof 函数 (将字符串转换为浮点数)

// 定义全局模型参数变量
RiskModelParameters g_ModelParams;

// 定义一套“出厂设置”的默认参数
const RiskModelParameters DEFAULT_PARAMS = {
    .written_flag = 42,
    .padding = {0, 0, 0},
    
    // 初始化新增的设备信息
    .station_id = 1.0f,
    .location = 1.0f,
    
    // 原始模型参数
    .overflow_base = 0.15f,
    .instability_base = 0.12f,
    .w_overflow_wave_height = 0.35f,
    .w_overflow_water_level = 0.45f,
    .w_instability_wave_height = 0.30f,
    .w_instability_water_level = 0.40f,
    .w_breach_overflow = 0.60f,
    .w_breach_instability = 0.40f,
    .w_total_overflow = 0.55f,
    .w_total_breach = 0.45f,
    .norm_wave_height_L = 4.4f,
    .norm_wave_height_U = 5.5f,
    .norm_water_level_L = 5.8f,
    .norm_water_level_U = 6.7f,
    .threshold_low = 0.3f,
    .threshold_medium = 0.6f,
    .threshold_high = 0.8f
};

// Model_Init 函数保持不变，它现在可以正常工作了
void Model_Init(void)
{
    // 1. 从Flash的起始地址读取标志位
    uint8_t flag = Flash_ReadByte(FLASH_STORE_ADDR);
    printf("Read Flag from Flash: %d\r\n", flag);

    // 2. 判断标志位是否为42
    if (flag != 42)
    {
        // Flash为空或数据损坏，加载并写入默认参数
        printf("Flag invalid. Loading default parameters and writing to Flash...\r\n");
        
        // 将默认参数复制到全局RAM变量中
        g_ModelParams = DEFAULT_PARAMS;
        
        // 将RAM中的参数写入Flash
        Flash_WriteModelParams(&g_ModelParams);
        
        printf("Default parameters written. System will auto-reset in 3 seconds...\r\n");
        
        // 延时，确保串口消息发送完毕
        Delay_ms(3000); 
        
        // 执行软件复位
        NVIC_SystemReset();
    }
    else
    {
        // Flash中有有效数据，直接读取到RAM
        printf("Flag is valid. Loading parameters from Flash...\r\n");
        Flash_ReadModelParams(&g_ModelParams);
        printf("Parameters loaded successfully.\r\n");
    }
}


/**
  * @brief  解析输入的"param"类型JSON字符串，并更新模型参数结构体
  * @param  json_string 指向包含参数JSON数据的字符串
  * @param  params_to_update 指向一个将被更新的 RiskModelParameters 结构体实例
  * @retval int 返回成功更新的参数数量，如果JSON解析失败则返回 0。
  */
int Parse_ParamInput(const char *json_string, RiskModelParameters *params_to_update)
{
    int update_count = 0;
    cJSON *root = cJSON_Parse(json_string);
    cJSON *item = NULL;

    // 如果JSON本身无法解析，则直接返回失败
    if (root == NULL) {
        printf("Error: Failed to parse param JSON string.\r\n");
        return 0;
    }

    // --- 1. 设备信息 ---
    item = cJSON_GetObjectItem(root, "station_id");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->station_id = (float)atof(item->valuestring);
        update_count++;
    }

    item = cJSON_GetObjectItem(root, "location");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->location = (float)atof(item->valuestring);
        update_count++;
    }

    // --- 2. 基础风险值 ---
    item = cJSON_GetObjectItem(root, "overflow_base");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->overflow_base = (float)atof(item->valuestring);
        update_count++;
    }

    item = cJSON_GetObjectItem(root, "instability_base");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->instability_base = (float)atof(item->valuestring);
        update_count++;
    }

    // --- 3. 漫顶风险权重 ---
    item = cJSON_GetObjectItem(root, "w_overflow_wave_height");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->w_overflow_wave_height = (float)atof(item->valuestring);
        update_count++;
    }

    item = cJSON_GetObjectItem(root, "w_overflow_water_level");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->w_overflow_water_level = (float)atof(item->valuestring);
        update_count++;
    }

    // --- 4. 失稳风险权重 ---
    item = cJSON_GetObjectItem(root, "w_instability_wave_height");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->w_instability_wave_height = (float)atof(item->valuestring);
        update_count++;
    }

    item = cJSON_GetObjectItem(root, "w_instability_water_level");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->w_instability_water_level = (float)atof(item->valuestring);
        update_count++;
    }

    // --- 5. 溃口风险权重 ---
    item = cJSON_GetObjectItem(root, "w_breach_overflow");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->w_breach_overflow = (float)atof(item->valuestring);
        update_count++;
    }

    item = cJSON_GetObjectItem(root, "w_breach_instability");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->w_breach_instability = (float)atof(item->valuestring);
        update_count++;
    }

    // --- 6. 综合风险权重 ---
    item = cJSON_GetObjectItem(root, "w_total_overflow");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->w_total_overflow = (float)atof(item->valuestring);
        update_count++;
    }

    item = cJSON_GetObjectItem(root, "w_total_breach");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->w_total_breach = (float)atof(item->valuestring);
        update_count++;
    }

    // --- 7. 归一化参数 ---
    item = cJSON_GetObjectItem(root, "norm_wave_height_L");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->norm_wave_height_L = (float)atof(item->valuestring);
        update_count++;
    }

    item = cJSON_GetObjectItem(root, "norm_wave_height_U");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->norm_wave_height_U = (float)atof(item->valuestring);
        update_count++;
    }

    item = cJSON_GetObjectItem(root, "norm_water_level_L");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->norm_water_level_L = (float)atof(item->valuestring);
        update_count++;
    }

    item = cJSON_GetObjectItem(root, "norm_water_level_U");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->norm_water_level_U = (float)atof(item->valuestring);
        update_count++;
    }

    // --- 8. 风险等级阈值 ---
    item = cJSON_GetObjectItem(root, "threshold_low");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->threshold_low = (float)atof(item->valuestring);
        update_count++;
    }

    item = cJSON_GetObjectItem(root, "threshold_medium");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->threshold_medium = (float)atof(item->valuestring);
        update_count++;
    }

    item = cJSON_GetObjectItem(root, "threshold_high");
    if (item != NULL && cJSON_IsString(item)) {
        params_to_update->threshold_high = (float)atof(item->valuestring);
        update_count++;
    }

    // 释放cJSON对象占用的内存
    cJSON_Delete(root);

    return update_count;
}

