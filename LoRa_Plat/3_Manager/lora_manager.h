#ifndef __LORA_MANAGER_H
#define __LORA_MANAGER_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================
//                    配置与宏定义
// ============================================================

// 调试打印开关 (1:开启, 0:关闭)
#define LORA_DEBUG_PRINT    1

// 缓冲区大小 (共享大缓存策略)
#define MGR_TX_BUF_SIZE     512
#define MGR_RX_BUF_SIZE     512

// 协议常量
#define PROTOCOL_HEAD_0     'C'
#define PROTOCOL_HEAD_1     'M'
#define PROTOCOL_TAIL_0     '\r'
#define PROTOCOL_TAIL_1     '\n'

// 控制字掩码
#define CTRL_MASK_TYPE      0x80 // Bit7: 0=Data, 1=ACK
#define CTRL_MASK_NEED_ACK  0x40 // Bit6: 1=Need ACK
#define CTRL_MASK_HAS_CRC   0x20 // Bit5: 1=Has CRC

// 超时设置 (ms)
#define TIMEOUT_TX_AUX      1000 // 等待模块空闲超时
#define TIMEOUT_WAIT_ACK    2000 // 等待ACK超时

// 最大重传次数
#define MAX_RETRY_COUNT     3

// ============================================================
//                    数据类型定义
// ============================================================

// 状态机状态
typedef enum {
    MGR_STATE_IDLE = 0,
    MGR_STATE_TX_CHECK_AUX, // 等待模块空闲
    MGR_STATE_TX_SENDING,   // 正在发送(DMA)
    MGR_STATE_WAIT_ACK,     // 等待ACK
} MgrState_t;

// 错误码
typedef enum {
    LORA_ERR_NONE = 0,
    LORA_ERR_TX_TIMEOUT,    // 发送超时(AUX一直忙)
    LORA_ERR_ACK_TIMEOUT,   // ACK超时
    LORA_ERR_CRC_FAIL,      // 接收CRC校验失败
    LORA_ERR_MEM_OVERFLOW   // 缓冲区溢出
} LoRaError_t;

// 回调函数原型
typedef void (*OnRxData_t)(uint8_t *data, uint16_t len, uint16_t src_id);
typedef void (*OnTxResult_t)(bool success); // true=成功(收到ACK或发送完), false=失败
typedef void (*OnError_t)(LoRaError_t err);

// Manager 配置结构体
typedef struct {
    uint16_t local_id;      // 本机ID
    bool     enable_crc;    // 是否开启CRC
    bool     enable_ack;    // 是否开启ACK机制
} ManagerConfig_t;

// Manager 核心对象 (单例)
typedef struct {
    // --- 共享内存 ---
    uint8_t TxBuffer[MGR_TX_BUF_SIZE];
    uint8_t RxBuffer[MGR_RX_BUF_SIZE];
    
    // --- 状态机变量 ---
    MgrState_t state;
    uint32_t   state_tick;  // 状态进入时间戳
    uint8_t    tx_seq;      // 发送序号
    uint8_t    retry_cnt;   // 当前重试次数
    
    // --- 接收解析变量 ---
    uint16_t   rx_len;      // 当前接收缓冲区有效数据长度
    
    // --- 配置与回调 ---
    ManagerConfig_t config;
    OnRxData_t      cb_on_rx;
    OnTxResult_t    cb_on_tx;
    OnError_t       cb_on_err;
    
} LoRa_Manager_t;

extern LoRa_Manager_t g_LoRaManager;

// ============================================================
//                    函数接口
// ============================================================

// 初始化
void Manager_Init(ManagerConfig_t *cfg, OnRxData_t on_rx, OnTxResult_t on_tx, OnError_t on_err);

// 发送数据 (非阻塞)
// payload: 用户数据
// len: 数据长度
// target_id: 目标设备ID (0xFFFF为广播)
// 返回: true=成功加入发送队列, false=忙或溢出
bool Manager_SendPacket(const uint8_t *payload, uint16_t len, uint16_t target_id);

// 主循环轮询 (驱动状态机)
void Manager_Run(void);

// 辅助: 动态开关CRC
void Manager_EnableCRC(bool enable);

#endif
