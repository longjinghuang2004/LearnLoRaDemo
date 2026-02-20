/**
  ******************************************************************************
  * @file    lora_manager_fsm.h
  * @author  LoRaPlat Team
  * @brief   LoRa 协议状态机定义 (V3.4.0 Decoupled)
  *          纯逻辑层，不依赖上层业务，通过返回值输出事件。
  ******************************************************************************
  */

#ifndef __LORA_MANAGER_FSM_H
#define __LORA_MANAGER_FSM_H

#include <stdint.h>
#include <stdbool.h>
#include "lora_manager_protocol.h"
#include "LoRaPlatConfig.h" // for LoRa_MsgID_t

// ============================================================
//                    1. 状态与事件定义
// ============================================================

/**
 * @brief FSM 内部状态枚举
 */
typedef enum {
    LORA_FSM_IDLE = 0,      // 空闲
    LORA_FSM_WAIT_ACK,      // 等待 ACK (重传计时中)
    LORA_FSM_ACK_DELAY,     // 发送 ACK 前的延时
    LORA_FSM_BROADCAST_RUN  // 广播盲发运行中
} LoRa_FSM_State_t;

/**
 * @brief FSM 输出事件类型
 */
typedef enum {
    FSM_EVT_NONE = 0,       // 无事发生
    FSM_EVT_TX_DONE,        // 发送流程结束 (成功/ACK收到)
    FSM_EVT_TX_TIMEOUT,     // 发送流程失败 (重传耗尽)
    // 注意：RX_DATA 事件通常由 ProcessRxPacket 直接处理或通过 Buffer 标志位处理，
    // 这里主要关注 TX 相关的异步结果。
} LoRa_FSM_EventType_t;

/**
 * @brief FSM 输出结构体 (Pull 模式)
 */
typedef struct {
    LoRa_FSM_EventType_t Event;  /*!< 事件类型 */
    LoRa_MsgID_t         MsgID;  /*!< 相关联的消息 ID (仅 TX 相关事件有效) */
} LoRa_FSM_Output_t;

// ============================================================
//                    2. 核心接口
// ============================================================

/**
 * @brief  初始化状态机
 * @param  cfg: 配置结构体指针 (依赖注入)
 */
void LoRa_Manager_FSM_Init(const LoRa_Config_t *cfg);

/**
 * @brief  运行状态机 (周期调用)
 * @param  scratch_buf: 共享工作区 (用于 TX 预览)
 * @param  scratch_len: 工作区大小
 * @return FSM 输出事件 (上层需根据此返回值触发回调)
 */
LoRa_FSM_Output_t LoRa_Manager_FSM_Run(uint8_t *scratch_buf, uint16_t scratch_len);

/**
 * @brief  处理接收到的数据包
 * @param  packet: 接收到的包
 * @return true=有效新包(需回调), false=重复包或ACK包(不回调)
 */
bool LoRa_Manager_FSM_ProcessRxPacket(const LoRa_Packet_t *packet);

/**
 * @brief  请求发送数据
 * @param  payload: 数据
 * @param  len: 长度
 * @param  target_id: 目标ID
 * @param  opt: 发送选项
 * @param  msg_id: 消息 ID
 * @param  scratch_buf: 栈缓冲区
 * @param  scratch_len: 缓冲区大小
 * @return true=成功入队
 */
bool LoRa_Manager_FSM_Send(const uint8_t *payload, uint16_t len, uint16_t target_id, LoRa_SendOpt_t opt,
                           LoRa_MsgID_t msg_id,
                           uint8_t *scratch_buf, uint16_t scratch_len);

/**
 * @brief  查询是否忙碌
 */
bool LoRa_Manager_FSM_IsBusy(void);

/**
 * @brief  获取距离下一次超时的剩余时间 (Tickless 核心)
 * @return 剩余毫秒数 (0=立即唤醒, INFINITE=无任务)
 */
uint32_t LoRa_Manager_FSM_GetNextTimeout(void);

#endif // __LORA_MANAGER_FSM_H
