#include "InputParser.h"
#include <string.h>
#include <stdio.h>

/**
  * @brief  (内部函数) 解析一个包含min和max成员的JSON对象
  */
static int parse_min_max_object(const cJSON *json_object, MinMaxFloat *output_struct)
{
    if (!cJSON_IsObject(json_object)) return 0;

    cJSON *min_item = cJSON_GetObjectItem(json_object, "min");
    cJSON *max_item = cJSON_GetObjectItem(json_object, "max");

    if (cJSON_IsNumber(min_item) && cJSON_IsNumber(max_item)) {
        output_struct->min = (float)min_item->valuedouble;
        output_struct->max = (float)max_item->valuedouble;
        return 1;
    }
    return 0;
}

/**
  * @brief  [新函数] 解析输入的"data"类型分包JSON字符串
  */
int Parse_BatchFrame(const char *json_string, BatchFrame *output_frame)
{
    cJSON *root = NULL;
    cJSON *item = NULL;
    cJSON *payload_obj = NULL;
    int success = 1;

    memset(output_frame, 0, sizeof(BatchFrame));

    root = cJSON_Parse(json_string);
    if (root == NULL) {
        printf("Error: Failed to parse JSON string.\r\n");
        return 0;
    }

    // 1. 解析 batch_id
    item = cJSON_GetObjectItem(root, "batch_id");
    if (cJSON_IsString(item)) {
        strncpy(output_frame->batch_id, item->valuestring, BATCH_ID_MAX_LEN - 1);
    } else { success = 0; }

    // 2. 解析 station_id
    if (success) {
        item = cJSON_GetObjectItem(root, "station_id");
        if (cJSON_IsString(item)) {
            strncpy(output_frame->station_id, item->valuestring, STATION_ID_MAX_LEN - 1);
        } else { success = 0; }
    }

    // 3. 解析 frame_index
    if (success) {
        item = cJSON_GetObjectItem(root, "frame_index");
        if (cJSON_IsNumber(item)) {
            output_frame->frame_index = item->valueint;
        } else { success = 0; }
    }

    // 4. 解析 total_frames
    if (success) {
        item = cJSON_GetObjectItem(root, "total_frames");
        if (cJSON_IsNumber(item)) {
            output_frame->total_frames = item->valueint;
        } else { success = 0; }
    }

    // 5. 解析 payload 对象
    if (success) {
        payload_obj = cJSON_GetObjectItem(root, "payload");
        if (cJSON_IsObject(payload_obj)) {
            // 5.1 解析 timestamp
            item = cJSON_GetObjectItem(payload_obj, "timestamp");
            if (cJSON_IsString(item)) {
                strncpy(output_frame->payload.timestamp, item->valuestring, TIMESTAMP_MAX_LEN - 1);
            } else { success = 0; }

            // 5.2 解析 wave_height
            if (success) {
                cJSON *wave_obj = cJSON_GetObjectItem(payload_obj, "wave_height");
                if (!parse_min_max_object(wave_obj, &output_frame->payload.wave_height)) { success = 0; }
            }

            // 5.3 解析 water_level
            if (success) {
                cJSON *water_obj = cJSON_GetObjectItem(payload_obj, "water_level");
                if (!parse_min_max_object(water_obj, &output_frame->payload.water_level)) { success = 0; }
            }
        } else {
            success = 0; // payload 对象不存在或类型错误
        }
    }
    
    if (!success) {
        printf("Error: Failed parsing batch frame JSON. Check format.\r\n");
    }

    cJSON_Delete(root);
    return success;
}
