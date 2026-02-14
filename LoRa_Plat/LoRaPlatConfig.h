#ifndef __LORA_PLAT_CONFIG_H
#define __LORA_PLAT_CONFIG_H

#include <stdint.h>
#include <stdbool.h>


// ============================================================
//                    版本号 v3.2.1
// ============================================================


// ============================================================
//                    0. 原port层参数配置
// ============================================================

// [删除] #define LORA_UART_BAUDRATE  115200
// [新增] 如下：
#define LORA_TARGET_BAUDRATE    9600  // 目标通信波特率





// 空中速率
typedef enum {
    LORA_RATE_0K3 = 0, LORA_RATE_1K2, LORA_RATE_2K4,
    LORA_RATE_4K8, LORA_RATE_9K6, LORA_RATE_19K2
} LoRa_AirRate_t;

// 发射功率
typedef enum {
    LORA_POWER_11dBm = 0, LORA_POWER_14dBm,
    LORA_POWER_17dBm, LORA_POWER_20dBm
} LoRa_Power_t;

// 传输模式
typedef enum {
    LORA_TMODE_TRANSPARENT = 0, // 透传模式 (默认)
    LORA_TMODE_FIXED       = 1  // 定向模式
} LoRa_TMode_t;

// ============================================================
//                    2. 默认出厂配置
// ============================================================
#define DEFAULT_LORA_CHANNEL    23              // 默认信道 23 (433MHz)
#define DEFAULT_LORA_RATE       LORA_RATE_19K2  // 默认高速
#define DEFAULT_LORA_POWER      LORA_POWER_11dBm // 默认低功耗
#define DEFAULT_LORA_TMODE      LORA_TMODE_TRANSPARENT
#define DEFAULT_LORA_TOKEN      0x00000000      // 默认安全令牌

// 特殊 ID 定义
#define LORA_ID_UNASSIGNED      0x0000          // 未分配 ID (新设备默认)
#define LORA_ID_BROADCAST       0xFFFF          // 广播 ID
#define LORA_HW_ADDR_DEFAULT    0x0000          // 默认物理地址 (全通)
#define LORA_GROUP_ID_DEFAULT   0x0000          // 默认组 ID (0表示无分组或默认组)

// ============================================================
//                    3. 配置结构体
// ============================================================
// [修改] Magic变更为0x5E以强制刷新旧Flash结构 (V3.0 Update)
#define LORA_CFG_MAGIC          0x5E            

typedef struct {
    uint8_t  magic;             // 有效标志
    
    // --- 身份识别 (Identity) ---
    uint32_t uuid;              // 32位唯一标识 (随机生成，终身不变)
    uint16_t net_id;            // 逻辑 ID (用于业务通信)
    uint16_t group_id;          // [新增] 组 ID (用于组播/逻辑分组)
    uint32_t token;             // 安全令牌 (可选)
    
    // --- 硬件参数 (Physical) ---
    uint16_t hw_addr;           // 物理地址 (写入模块AT+ADDR，通常为0)
    uint8_t  channel;           // 信道 (0-31)
    uint8_t  power;             // 功率 (0-3)
    uint8_t  air_rate;          // 空速 (0-5)
    uint8_t  tmode;             // 传输模式 (0=透传, 1=定向)
    
    uint8_t  padding[1];        // 对齐保留 (调整padding以保持4字节对齐)
} LoRa_Config_t;

// ============================================================
//                    4. LoRaPlat内部系统参数配置 (LoRaPlat System Parameters)
// ============================================================

/**
 * @brief ACK 确认机制开关
 * @note  true:  开启。发送数据时，如果设置了需要ACK，协议栈会等待接收方的确认包。
 *               如果超时未收到，会自动触发重传。适合对可靠性要求高的场景（如报警、控制指令）。
 *        false: 关闭。发送即忘（Fire and Forget）。适合对实时性要求高、允许少量丢包
 *               或通过上层业务逻辑保证可靠性的场景（如高频传感器数据上报）。
 */
#define LORA_ENABLE_ACK         true

/**
 * @brief CRC16 数据校验开关
 * @note  true:  开启。协议栈会在数据包尾部附加2字节的CRC校验码。接收方会计算校验码，
 *               如果不匹配则直接丢弃该包，防止因无线干扰导致的乱码数据进入业务层。
 *        false: 关闭。节省2字节开销，但应用层可能收到错误的数据。
 */
#define LORA_ENABLE_CRC         true

/**
 * @brief 调试日志开关
 * @note  1: 开启。通过 OSAL_Log 输出日志。
 *        0: 关闭。所有日志语句在编译阶段被移除，零开销。
 */
#define LORA_DEBUG_PRINT        1 

/**
 * @brief ACK 回复延时 (单位: ms)
 * @note  从机在收到数据后，不会立即回复 ACK，而是等待这段时间再发。
 *        原因：LoRa 模块是半双工的，主机发送完数据后，需要几十毫秒的时间从 TX 模式
 *        切换回 RX 模式。如果从机回复太快，主机还没准备好接收，ACK 就会丢失。
 *        建议值：50~200ms。
 */
#define LORA_ACK_DELAY_MS       100

/**
 * @brief 发送超时时间 (单位: ms)
 * @note  指数据写入串口并启动 DMA 发送后，等待“发送完成”信号的最长时间。
 *        如果超过这个时间模块还没发完（比如模块死机或 AUX 信号异常），
 *        协议栈会判定发送失败并复位状态机，防止程序卡死。
 */
#define LORA_TX_TIMEOUT_MS      1000

/**
 * @brief 等待 ACK 超时时间 (单位: ms)
 * @note  主机发送数据后，等待从机回复 ACK 的最长耐心时间。
 *        这个时间必须大于 (空中传输时间 + LORA_ACK_DELAY_MS + 从机处理时间)。
 *        如果超时未收到 ACK，协议栈会触发重传逻辑。
 */
#define LORA_ACK_TIMEOUT_MS     2000

/**
 * @brief 最大重传次数
 * @note  当等待 ACK 超时后，协议栈会自动重试发送的次数。
 *        例如设置为 3，则总共最多尝试发送 1(初次) + 3(重试) = 4 次。
 *        如果重传这么多次依然失败，回调函数会报告 LORA_ERR_ACK_TIMEOUT 错误。
 */
#define LORA_MAX_RETRY          3

/**
 * @brief Flash 参数保存开关
 * @note  1: 开启。支持掉电保存配置 (NetID, Channel 等)。
 *           初始化时会尝试从 Flash 读取参数覆盖默认值。
 *           收到 CMD:CFG 指令时会将新参数写入 Flash。但需要用户
 *					 自己实现非易失性存储
 *        0: 关闭。纯内存模式。
 *           每次上电都强制使用代码中的默认宏 (DEFAULT_LORA_xxx)。
 *           适合批量量产（通过修改 Hex/宏）或防止参数被意外篡改。
 */
#define LORA_ENABLE_FLASH_SAVE  1


// ============================================================
//                    6. 缓冲区配置 (Buffer Config)
// ============================================================
#define MGR_TX_BUF_SIZE     512   // 发送队列大小
#define MGR_RX_BUF_SIZE     512   // 接收缓冲区大小

#endif // __LORA_PLAT_CONFIG_H
