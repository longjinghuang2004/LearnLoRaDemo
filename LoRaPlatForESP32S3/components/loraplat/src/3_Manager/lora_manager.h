/**
  ******************************************************************************
  * @file    lora_manager.h
  * @author  LoRaPlat Team
  * @brief   LoRa 逻辑链路层管理器 (V3.4.0 Decoupled)
  ******************************************************************************
  */

#ifndef __LORA_MANAGER_H
#define __LORA_MANAGER_H

#include "LoRaPlatConfig.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 回调接口定义
// ============================================================

/**
 * @brief Manager 层回调接口
 *        Service 层需实现此接口以接收数据和事件
 */
typedef struct {
    /**
     * @brief 接收数据回调
     * @param data 数据指针
     * @param len 数据长度
     * @param src_id 源设备 ID
     */
    void (*OnRecv)(uint8_t *data, uint16_t len, uint16_t src_id);

    /**
     * @brief 发送结果回调
     * @param msg_id 消息 ID
     * @param success true=成功(ACK/Done), false=失败(Timeout)
     */
    void (*OnTxResult)(LoRa_MsgID_t msg_id, bool success);
    
} LoRa_Manager_Callback_t;

/** 
 * @brief 加密/解密算法接口结构体 
 */
typedef struct {
    uint16_t (*Encrypt)(const uint8_t *plain, uint16_t len, uint8_t *cipher);
    uint16_t (*Decrypt)(const uint8_t *cipher, uint16_t len, uint8_t *plain);
} LoRa_Cipher_t;

// ============================================================
//                    2. 核心接口
// ============================================================

/**
 * @brief  初始化管理器
 * @param  cfg: 配置结构体指针
 * @param  cb:  回调结构体指针 (内部会保存副本或指针，请确保生命周期)
 */
void LoRa_Manager_Init(const LoRa_Config_t *cfg, const LoRa_Manager_Callback_t *cb);

/**
 * @brief  注册安全算法 (可选)
 */
void LoRa_Manager_RegisterCipher(const LoRa_Cipher_t *cipher);

/**
 * @brief  主循环 (需周期性调用)
 */
void LoRa_Manager_Run(void);

/**
 * @brief  发送数据 (非阻塞)
 * @return >0: 消息 ID, 0: 失败
 */
LoRa_MsgID_t LoRa_Manager_Send(const uint8_t *payload, uint16_t len, uint16_t target_id, LoRa_SendOpt_t opt);

/**
 * @brief  查询是否忙碌
 */
bool LoRa_Manager_IsBusy(void);

/**
 * @brief  获取建议休眠时长 (Tickless)
 */
uint32_t LoRa_Manager_GetSleepDuration(void);

#endif // __LORA_MANAGER_H
