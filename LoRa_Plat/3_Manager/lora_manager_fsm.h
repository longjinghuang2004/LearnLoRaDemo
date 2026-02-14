/**
  ******************************************************************************
  * @file    lora_manager_fsm.h
  * @author  LoRaPlat Team
  * @brief   LoRa 协议状态机定义 (V3.3.3 可靠传输版)
  ******************************************************************************
  */

#ifndef __LORA_MANAGER_FSM_H
#define __LORA_MANAGER_FSM_H

#include <stdint.h>
#include <stdbool.h>
#include "lora_manager_protocol.h"

// ============================================================
//                    1. 状态定义
// ============================================================

typedef enum {
    LORA_FSM_IDLE = 0,      // 空闲
    LORA_FSM_TX_RUNNING,    // 正在发送 (等待硬件完成) - 目前简化未用
    LORA_FSM_WAIT_ACK,      // 等待 ACK (重传计时中)
    LORA_FSM_ACK_DELAY      // 发送 ACK 前的延时
} LoRa_FSM_State_t;

// ============================================================
//                    2. 核心接口
// ============================================================

/**
 * @brief  初始化状态机
 */
void LoRa_Manager_FSM_Init(void);

/**
 * @brief  运行状态机 (周期调用)
 * @param  scratch_buf: 共享工作区 (用于 TX 预览)
 * @param  scratch_len: 工作区大小
 */
void LoRa_Manager_FSM_Run(uint8_t *scratch_buf, uint16_t scratch_len);

/**
 * @brief  处理接收到的数据包
 * @param  packet: 接收到的包
 * @return true=有效新包(需回调), false=重复包或ACK包(不回调)
 */
// 【修改】返回值改为 bool，指示是否需要回调上层
bool LoRa_Manager_FSM_ProcessRxPacket(const LoRa_Packet_t *packet);

/**
 * @brief  请求发送数据
 * @param  payload: 数据
 * @param  len: 长度
 * @param  target_id: 目标ID
 * @param  scratch_buf: 栈缓冲区 (用于序列化)
 * @param  scratch_len: 缓冲区大小
 * @return true=成功入队
 */
bool LoRa_Manager_FSM_Send(const uint8_t *payload, uint16_t len, uint16_t target_id,
                           uint8_t *scratch_buf, uint16_t scratch_len);

/**
 * @brief  查询是否忙碌
 */
bool LoRa_Manager_FSM_IsBusy(void);

#endif // __LORA_MANAGER_FSM_H
