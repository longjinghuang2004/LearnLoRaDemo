/**
  ******************************************************************************
  * @file    LoRaPlatConfig.h
  * @author  LoRaPlat Team
  * @version V3.4.6
  * @date    2023-10-27
  * @brief   LoRaPlat 全局配置头文件
  *          本文件集中管理所有层级（Port, Driver, Manager, Service）的可配置参数。
  *          用户在移植或调优时，通常只需修改本文件即可。
  ******************************************************************************
  */

#ifndef __LORA_PLAT_CONFIG_H
#define __LORA_PLAT_CONFIG_H

#include <stdint.h>
#include <stdbool.h>

// ============================================================================
// 1. 系统与调试配置 (System & Debug)
// ============================================================================

/**
 * @brief  调试日志开关
 * @note   1: 开启。通过 OSAL_Log 输出日志 (需适配 lora_osal.c)。
 *         0: 关闭。所有日志语句在编译阶段被移除，零开销。
 * @used_in lora_osal.h (LORA_LOG 宏)
 */
#define LORA_DEBUG_PRINT        1 

/**
 * @brief  无限超时常量定义
 * @note   用于 OSAL 和 FSM 的超时判断，通常无需修改。
 * @used_in lora_manager_fsm.c, lora_service.c
 */
#define LORA_TIMEOUT_INFINITE   0xFFFFFF


// ============================================================================
// 2. 物理层配置 (Physical Layer - Port & Driver)
// ============================================================================

/**
 * @brief  目标通信波特率
 * @note   MCU 与 LoRa 模组之间的 UART 通信速率。
 *         ATK-LORA-01 默认支持 9600, 115200 等。
 *         注意：波特率越高，传输越快，但对 MCU 的中断响应要求越高。
 * @used_in lora_driver_core.c, lora_port.c
 */
#define LORA_TARGET_BAUDRATE    9600

/**
 * @brief  Port 层 DMA 接收缓冲区大小 (Bytes)
 * @note   这是硬件层的原始接收缓冲。必须大于最大的预期单次突发数据包长度。
 *         建议值：512 或 1024。
 * @used_in lora_port_stm32f10x.c (s_DmaRxBuf)
 */
#define LORA_PORT_DMA_RX_SIZE   512

/**
 * @brief  Port 层 DMA 发送缓冲区大小 (Bytes)
 * @note   这是硬件层的原始发送缓冲。
 * @used_in lora_port_stm32f10x.c (s_DmaTxBuf)
 */
#define LORA_PORT_DMA_TX_SIZE   512


// ============================================================================
// 3. 协议与缓冲区配置 (Protocol & Manager Buffer)
// ============================================================================

/**
 * @brief  最大负载长度 (Payload Length)
 * @note   LoRa 物理包通常限制在 255 字节以内。
 *         扣除协议头 (Head, Ctrl, Seq, Addr, CRC, Tail) 约 12 字节，
 *         建议最大负载不超过 200 字节。
 * @used_in lora_manager_protocol.h, lora_manager_fsm.c
 */
#define LORA_MAX_PAYLOAD_LEN    200

/**
 * @brief  CRC16 数据校验开关
 * @note   true:  开启。协议栈会在数据包尾部附加 2 字节 CRC。
 *         false: 关闭。节省开销，但可能收到乱码。
 * @used_in lora_manager_protocol.c
 */
#define LORA_ENABLE_CRC         true

/**
 * @brief  Manager 层发送队列大小 (Bytes)
 * @note   这是软件层的环形缓冲区 (RingBuffer)，用于缓存待发送的应用数据。
 *         若应用层发送频率高于 LoRa 空中速率，需增大此值。
 * @used_in lora_manager_buffer.c (s_TxBufArr)
 */
#define MGR_TX_BUF_SIZE         512

/**
 * @brief  Manager 层接收队列大小 (Bytes)
 * @note   这是软件层的环形缓冲区，用于缓存从 Port 层搬运上来的数据。
 * @used_in lora_manager_buffer.c (s_RxBufArr)
 */
#define MGR_RX_BUF_SIZE         512

/**
 * @brief  ACK 专用队列大小 (Bytes)
 * @note   ACK 包优先级最高，使用独立的小队列，防止被普通数据阻塞。
 *         64 字节通常足够存放 3-4 个 ACK 包。
 * @used_in lora_manager_buffer.c (s_AckBufArr)
 */
#define ACK_QUEUE_SIZE          64


// ============================================================================
// 4. 可靠性与时序配置 (Reliability & Timing - FSM)
// ============================================================================

/**
 * @brief  发送超时时间 (ms)
 * @note   数据写入串口并启动 DMA 后，等待“发送完成”信号的最长时间。
 *         防止因硬件故障导致状态机卡死。
 * @used_in lora_manager_fsm.c
 */
#define LORA_TX_TIMEOUT_MS      1000

/**
 * @brief  ACK 回复延时 (ms)
 * @note   接收方收到数据后，等待多久再回复 ACK。
 *         用于给发送方预留“收发切换”的时间 (半双工特性)。
 *         建议值：50~200ms。
 * @used_in lora_manager_fsm.c
 */
#define LORA_ACK_DELAY_MS       100

/**
 * @brief  等待 ACK 超时时间 (ms)
 * @note   发送方发出数据后，等待 ACK 的最长耐心时间。
 *         超时后将触发重传。
 * @used_in lora_manager_fsm.c
 */
#define LORA_ACK_TIMEOUT_MS     2000

/**
 * @brief  最大重传次数
 * @note   超时未收到 ACK 时的最大重试次数。
 * @used_in lora_manager_fsm.c
 */
#define LORA_MAX_RETRY          3

/**
 * @brief  重传基础间隔时间 (ms)
 * @note   重传时的基础退避时间。实际间隔 = Base + (RetryCount * 500) + Random(0~500)。
 * @used_in lora_manager_fsm.c
 */
#define LORA_RETRY_INTERVAL_MS  1500

/**
 * @brief  接收去重表大小 (条目数)
 * @note   记录最近 N 个包的 ID 以防止重复处理。
 *         采用 LRU 策略。每条目约占 8 字节 RAM。
 * @used_in lora_manager_fsm.c
 */
#define LORA_DEDUP_MAX_COUNT    4

/**
 * @brief  去重记录有效期 (ms)
 * @note   超过此时间的去重记录将被视为过期，可以被新包覆盖。
 * @used_in lora_manager_fsm.c
 */
#define LORA_DEDUP_TTL_MS       5000

/**
 * @brief  广播包盲发次数
 * @note   广播包无 ACK，通过多次发送提高送达率。
 * @used_in lora_manager_fsm.c
 */
#define LORA_BROADCAST_REPEAT   3

/**
 * @brief  广播包发送间隔 (ms)
 * @note   连续广播包之间的留白时间。
 * @used_in lora_manager_fsm.c
 */
#define LORA_BROADCAST_INTERVAL 50

/**
 * @brief  去重记录有效期 (ms)
 * @note   超过此时间的去重记录将被视为过期，可以被新包覆盖。
 * @used_in lora_manager_fsm.c
 */
#define LORA_DEDUP_TTL_MS       5000


// ============================================================================
// 5. 业务与高级功能配置 (Service & Features)
// ============================================================================

/**
 * @brief  OTA 远程配置开关
 * @note   1: 开启。支持通过 CMD 指令远程修改参数。
 *         0: 关闭。节省 Flash 空间。
 * @used_in lora_service.c
 */
#define LORA_ENABLE_OTA_CFG     1

/**
 * @brief  Flash 参数保存开关
 * @note   1: 开启。支持掉电保存配置 (需适配 SaveConfig 回调)。
 *         0: 关闭。每次上电使用代码默认值。
 * @used_in lora_service_config.c
 */
#define LORA_ENABLE_FLASH_SAVE  1

/**
 * @brief  软重启等待时间 (ms)
 * @note   收到 OTA 重启指令或回复 ACK 后，等待多久执行复位。
 *         给数据发送留出缓冲时间。
 * @used_in lora_service.c
 */
#define LORA_REBOOT_DELAY_MS    3000

/**
 * @brief  驱动忙碌监控阈值 (ms)
 * @note   如果驱动层连续忙碌超过此时间，Monitor 将触发自愈重启。
 * @used_in lora_service_monitor.c
 */
#define LORA_MONITOR_BUSY_THRESHOLD_MS  10000


// ============================================================================
// 6. 默认出厂参数 (Factory Defaults)
// ============================================================================
// 当 Flash 为空或执行恢复出厂设置时，将使用以下参数

#define DEFAULT_LORA_CHANNEL    23              /*!< 默认信道 23 (433MHz) */
#define DEFAULT_LORA_RATE       LORA_RATE_19K2  /*!< 默认空速 19.2kbps */
#define DEFAULT_LORA_POWER      LORA_POWER_20dBm /*!< 默认功率 20dBm */
#define DEFAULT_LORA_TMODE      LORA_TMODE_TRANSPARENT /*!< 默认透传模式 */
#define DEFAULT_LORA_TOKEN      0x00000000      /*!< 默认安全令牌 */

// 特殊 ID 定义
#define LORA_ID_UNASSIGNED      0x0000          /*!< 未分配 ID */
#define LORA_ID_BROADCAST       0xFFFF          /*!< 广播 ID */
#define LORA_HW_ADDR_DEFAULT    0x0000          /*!< 默认物理地址 */
#define LORA_GROUP_ID_DEFAULT   0x0000          /*!< 默认组 ID */


// ============================================================================
// 7. 类型定义 (Type Definitions)
// ============================================================================
// 以下类型因被多个模块引用，且与配置紧密相关，故保留在此处

/** @brief 消息 ID 类型 (0 为无效 ID) */
typedef uint16_t LoRa_MsgID_t;

/** @brief 发送选项结构体 */
typedef struct {
    bool NeedAck; /*!< true=需要ACK(可靠), false=不需要(不可靠) */
} LoRa_SendOpt_t;

/** @brief 空中速率枚举 */
typedef enum {
    LORA_RATE_0K3 = 0, LORA_RATE_1K2, LORA_RATE_2K4,
    LORA_RATE_4K8, LORA_RATE_9K6, LORA_RATE_19K2
} LoRa_AirRate_t;

/** @brief 发射功率枚举 */
typedef enum {
    LORA_POWER_11dBm = 0, LORA_POWER_14dBm,
    LORA_POWER_17dBm, LORA_POWER_20dBm
} LoRa_Power_t;

/** @brief 传输模式枚举 */
typedef enum {
    LORA_TMODE_TRANSPARENT = 0, /*!< 透传模式 (软件过滤地址) */
    LORA_TMODE_FIXED       = 1  /*!< 定点模式 (硬件过滤地址) */
} LoRa_TMode_t;

/** @brief 配置结构体 Magic Number (用于校验 Flash 数据有效性) */
#define LORA_CFG_MAGIC          0x5E            

/** @brief 核心配置结构体 (需保存到 Flash) */
typedef struct {
    uint8_t  magic;             /*!< 有效标志 (LORA_CFG_MAGIC) */
    
    // --- 身份识别 (Identity) ---
    uint32_t uuid;              /*!< 32位唯一标识 */
    uint16_t net_id;            /*!< 逻辑 ID (业务通信用) */
    uint16_t group_id;          /*!< 组 ID (组播用) */
    uint32_t token;             /*!< 安全令牌 (CMD鉴权用) */
    
    // --- 硬件参数 (Physical) ---
    uint16_t hw_addr;           /*!< 物理地址 (AT+ADDR) */
    uint8_t  channel;           /*!< 信道 (0-31) */
    uint8_t  power;             /*!< 功率 (0-3) */
    uint8_t  air_rate;          /*!< 空速 (0-5) */
    uint8_t  tmode;             /*!< 模式 (0=透传, 1=定点) */
    
    uint8_t  padding[1];        /*!< 4字节对齐保留位 */
} LoRa_Config_t;

#endif  //__LORA_PLAT_CONFIG_H
