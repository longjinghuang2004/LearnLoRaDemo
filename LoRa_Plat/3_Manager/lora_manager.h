/**
  ******************************************************************************
  * @file    lora_manager.h
  * @author  LoRaPlat Team
  * @brief   Layer 3: 协议管理层 (Protocol Manager)
  * @note    本层负责私有协议的封包解包、可靠性传输(ARQ)和地址过滤。
  ******************************************************************************
  */

#ifndef __LORA_MANAGER_H
#define __LORA_MANAGER_H

#include <stdint.h>
#include <stdbool.h>
#include "lora_driver.h" // 依赖 Driver 层提供的 Result 定义

// ============================================================
//                    1. 配置宏 (Configuration)
// ============================================================

#define MGR_TX_BUF_SIZE     256     ///< 发送缓冲区大小
#define MGR_RX_BUF_SIZE     256     ///< 接收缓冲区大小
#define MGR_ACK_TIMEOUT_MS  2000    ///< 等待 ACK 的超时时间
#define MGR_MAX_RETRY       3       ///< 最大重传次数

// ============================================================
//                    2. 类型定义 (Types)
// ============================================================

/**
 * @brief Manager 内部状态机
 */
typedef enum {
    MGR_STATE_IDLE = 0,         ///< 空闲
    MGR_STATE_TX_WAIT_DRIVER,   ///< 已提交给 Driver，等待物理发送完成
    MGR_STATE_WAIT_ACK,         ///< 物理发送完成，等待逻辑 ACK
} Mgr_State_t;

/**
 * @brief 接收数据回调函数原型
 * @param data   指向 Payload 的指针
 * @param len    Payload 长度
 * @param src_id 源设备 ID
 * @param rssi   信号强度 (预留)
 */
typedef void (*Mgr_OnRecv_t)(uint8_t *data, uint16_t len, uint16_t src_id, int16_t rssi);

/**
 * @brief 发送结果回调函数原型
 * @param success true=发送成功(收到ACK或无需ACK), false=失败
 */
typedef void (*Mgr_OnTxResult_t)(bool success);

/**
 * @brief 错误报告回调函数原型
 * @param err 错误码 (如 ACK超时, CRC错误等)
 */
typedef void (*Mgr_OnError_t)(LoRa_Result_t err);

/**
 * @brief Manager 控制块
 */
typedef struct {
    // --- 身份信息 ---
    uint16_t local_id;      ///< 本机逻辑 ID
    uint16_t group_id;      ///< 本机组 ID
    
    // --- 回调接口 ---
    Mgr_OnRecv_t     cb_on_recv;
    Mgr_OnTxResult_t cb_on_tx;
    Mgr_OnError_t    cb_on_err;
    
    // --- 运行时状态 ---
    Mgr_State_t state;
    uint32_t    state_tick;     ///< 状态进入时间戳
    uint8_t     tx_seq;         ///< 当前发送序列号
    uint8_t     retry_cnt;      ///< 当前重试次数
    
    // --- 缓冲区 ---
    uint8_t  tx_buf[MGR_TX_BUF_SIZE];
    uint16_t tx_len;            ///< 当前待发送包的总长度
    
    uint8_t  rx_buf[MGR_RX_BUF_SIZE];
    uint16_t rx_len;            ///< 当前接收缓冲区内的数据长度
    
} LoRa_Manager_t;

// ============================================================
//                    3. 核心接口 (API)
// ============================================================

/**
 * @brief  初始化协议层
 * @param  local_id: 本机 ID
 * @param  group_id: 组 ID
 * @param  on_recv:  接收回调
 * @param  on_tx:    发送结果回调
 * @param  on_err:   错误回调
 */
void Manager_Init(uint16_t local_id, uint16_t group_id,
                  Mgr_OnRecv_t on_recv, Mgr_OnTxResult_t on_tx, Mgr_OnError_t on_err);

/**
 * @brief  协议层心跳 (必须在主循环调用)
 * @note   负责处理接收解析、ACK 超时检查、重传逻辑
 */
void Manager_Run(void);

/**
 * @brief  发送应用数据
 * @param  payload:   数据内容
 * @param  len:       数据长度
 * @param  target_id: 目标 ID (0xFFFF为广播)
 * @param  need_ack:  是否需要对方回复 ACK
 * @return LORA_OK: 请求已受理
 *         LORA_ERR_BUSY: 正在发送上一包，拒绝
 *         LORA_ERR_PARAM: 数据过长
 */
LoRa_Result_t Manager_Send(const uint8_t *payload, uint16_t len, uint16_t target_id, bool need_ack);

/**
 * @brief  检查协议层是否空闲
 * @return true=空闲 (可休眠), false=忙
 */
bool Manager_IsIdle(void);

#endif // __LORA_MANAGER_H
