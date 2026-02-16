/**
  ******************************************************************************
  * @file    lora_manager.h
  * @author  LoRaPlat Team
  * @brief   LoRa 逻辑链路层管理器 (V3.3.4 Security Enhanced)
  *          负责数据包的调度、FSM 驱动以及安全加密钩子。
  ******************************************************************************
  */

#ifndef __LORA_MANAGER_H
#define __LORA_MANAGER_H

#include "LoRaPlatConfig.h"
#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    1. 宏定义与类型
// ============================================================

/** @brief 定义无穷大时间 (表示无定时任务) */
#define LORA_TIMEOUT_INFINITE   0xFFFFFFFF

/** @brief 接收数据回调函数原型 */
typedef void (*LoRa_OnRxData_t)(uint8_t *data, uint16_t len, uint16_t src_id);

/** 
 * @brief 加密/解密算法接口结构体 
 * @note  用户可注册自定义算法 (如 XOR, AES, ChaCha20)。
 *        注意：仅对 Payload 进行加密，协议头保持明文以支持路由和去重。
 */
typedef struct {
    /**
     * @brief  加密函数
     * @param  plain: 明文数据输入
     * @param  len:   数据长度
     * @param  cipher: 密文数据输出 (缓冲区大小保证至少为 LORA_MAX_PAYLOAD_LEN)
     * @return 加密后的长度 (若算法导致长度增加，需确保不超过 MAX_PAYLOAD)
     */
    uint16_t (*Encrypt)(const uint8_t *plain, uint16_t len, uint8_t *cipher);

    /**
     * @brief  解密函数
     * @param  cipher: 密文数据输入
     * @param  len:    数据长度
     * @param  plain:  明文数据输出
     * @return 解密后的长度
     */
    uint16_t (*Decrypt)(const uint8_t *cipher, uint16_t len, uint8_t *plain);
} LoRa_Cipher_t;

// ============================================================
//                    2. 核心接口
// ============================================================

/**
 * @brief  初始化管理器
 * @param  cfg: 配置结构体指针
 * @param  on_rx: 接收回调
 */
void LoRa_Manager_Init(const LoRa_Config_t *cfg, LoRa_OnRxData_t on_rx);

/**
 * @brief  注册安全算法 (可选)
 * @param  cipher: 算法接口指针 (NULL 表示注销)
 */
void LoRa_Manager_RegisterCipher(const LoRa_Cipher_t *cipher);

/**
 * @brief  主循环 (需周期性调用)
 */
void LoRa_Manager_Run(void);

/**
 * @brief  发送数据 (非阻塞)
 * @param  payload: 数据内容
 * @param  len: 数据长度
 * @param  target_id: 目标逻辑 ID
 * @param  opt: 发送选项 [新增]
 * @return true=成功入队/发送, false=忙或错误
 */
bool LoRa_Manager_Send(const uint8_t *payload, uint16_t len, uint16_t target_id, LoRa_SendOpt_t opt);


/**
 * @brief  查询是否忙碌
 */
bool LoRa_Manager_IsBusy(void);

/**
 * @brief  获取建议休眠时长 (Tickless)
 */
uint32_t LoRa_Manager_GetSleepDuration(void);

#endif // __LORA_MANAGER_H
