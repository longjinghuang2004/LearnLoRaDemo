#include "lora_driver.h"
#include "lora_port.h"
#include "lora_osal.h"
#include <string.h>
#include <stdio.h>

// ============================================================
//                    1. 私有宏与定义
// ============================================================

// ATK-LORA-01 硬件规定：配置模式(MD0=1)下固定使用 115200
#define ATK_LORA_CONFIG_BAUDRATE    115200

// 防止用户忘记在 Config.h 中定义新宏，提供默认值
#ifndef LORA_TARGET_BAUDRATE
    #define LORA_TARGET_BAUDRATE    9600
#endif

// ============================================================
//                    2. 内部辅助函数
// ============================================================

/**
 * @brief  将波特率数值转换为 AT+UART 指令的参数
 * @param  baudrate: 波特率数值 (e.g. 9600)
 * @return 参数值 (0-7), 默认返回 3 (9600)
 */
static int _GetBaudParam(uint32_t baudrate) {
    switch(baudrate) {
        case 1200:   return 0;
        case 2400:   return 1;
        case 4800:   return 2;
        case 9600:   return 3;
        case 19200:  return 4;
        case 38400:  return 5;
        case 57600:  return 6;
        case 115200: return 7;
        default:     return 3; // 默认 9600
    }
}

/**
 * @brief  阻塞式发送 AT 指令并等待响应
 * @param  cmd: AT指令字符串 (e.g. "AT\r\n")
 * @param  expect: 期望收到的响应子串 (e.g. "OK")
 * @param  timeout: 超时时间 (ms)
 * @return true=成功, false=超时
 */
static bool _SendAT(const char *cmd, const char *expect, uint32_t timeout) {
    // 1. 清空接收缓存，防止旧数据干扰
    Port_ClearRxBuffer();
    
    // 2. 发送指令
    Port_WriteData((uint8_t*)cmd, strlen(cmd));
    
    // 3. 等待响应
    uint32_t start = OSAL_GetTick();
    char rx_buf[128];
    uint16_t rx_idx = 0;
    
    while (OSAL_GetTick() - start < timeout) {
        uint8_t byte;
        // 逐字节读取
        if (Port_ReadData(&byte, 1) > 0) {
            rx_buf[rx_idx++] = byte;
            if (rx_idx >= 127) rx_idx = 127; // 防止溢出
            rx_buf[rx_idx] = '\0';           // 保持字符串结尾
            
            // 检查是否包含期望的字符串
            if (strstr(rx_buf, expect)) {
                OSAL_DelayMs(20); // 稍作延时确保模块后续字符发完(如\r\n)
                Port_ClearRxBuffer();
                return true;
            }
        }
    }
    return false;
}

// ============================================================
//                    3. 核心接口实现
// ============================================================

/**
 * @brief  驱动初始化 (阻塞式 - 智能波特率切换版)
 * @note   逻辑流程：
 *         1. 进入配置模式 (MD0=1)
 *         2. MCU 切到 115200 (匹配模块配置模式)
 *         3. 下发 AT 指令 (包含设置模块通信波特率为 LORA_TARGET_BAUDRATE)
 *         4. 退出配置模式 (MD0=0)
 *         5. MCU 切到 LORA_TARGET_BAUDRATE (匹配模块通信模式)
 */
bool Drv_Init(const LoRa_Config_t *cfg) {
    // 1. 端口底层初始化
    Port_Init(ATK_LORA_CONFIG_BAUDRATE); 
    
    LORA_LOG("[DRV] Init Start. Target Baud: %d\r\n", LORA_TARGET_BAUDRATE);

    // 2. 进入配置模式 (MD0=1)
    // 根据手册，此时模块强制工作在 115200, 8N1
    Port_SetMD0(true);
    OSAL_DelayMs(600); // 等待模式切换稳定
    
    // 3. 【关键步骤】强制 MCU 串口切换到 115200
    // 无论用户想用什么波特率，配置阶段必须用 115200
    Port_ReInitUart(ATK_LORA_CONFIG_BAUDRATE);
    OSAL_DelayMs(100); 
    
    // 4. 握手检查
    bool link_ok = false;
    for (int i = 0; i < 3; i++) {
        Port_ClearRxBuffer();
        if (_SendAT("AT\r\n", "OK", 200)) {
            link_ok = true;
            LORA_LOG("[DRV] Handshake OK at 115200 (Config Mode)\r\n");
            break;
        }
        LORA_LOG("[DRV] Handshake Fail %d/3\r\n", i+1);
        OSAL_DelayMs(100);
    }
    
    if (!link_ok) {
        LORA_LOG("[DRV] Fatal: Module No Response!\r\n");
        // 尝试切回目标波特率，死马当活马医，方便后续调试
        Port_ReInitUart(LORA_TARGET_BAUDRATE);
        return false;
    }
    
    // 5. 应用配置参数
    char cmd[64];
    bool cfg_ok = true;
    
    // 5.1 设置地址
    Drv_GetAtCmd_Addr(cfg->hw_addr, cmd);
    if (!_SendAT(cmd, "OK", 500)) cfg_ok = false;
    
    // 5.2 设置空速和信道
    Drv_GetAtCmd_Rate(cfg->channel, cfg->air_rate, cmd);
    if (!_SendAT(cmd, "OK", 500)) cfg_ok = false;
    
    // 5.3 设置功率
    Drv_GetAtCmd_Power(cfg->power, cmd);
    if (!_SendAT(cmd, "OK", 500)) cfg_ok = false;
    
    // 5.4 设置传输模式 (透传/定向)
    if (!_SendAT(Drv_GetAtCmd_Mode(cfg->tmode), "OK", 500)) cfg_ok = false;

    // 5.5 【关键步骤】设置模块的通信波特率
    // 告诉模块：退出配置模式后，请使用 LORA_TARGET_BAUDRATE (例如 9600)
    int baud_param = _GetBaudParam(LORA_TARGET_BAUDRATE);
    sprintf(cmd, "AT+UART=%d,0\r\n", baud_param);
    
    if (!_SendAT(cmd, "OK", 500)) {
        LORA_LOG("[DRV] Set UART Fail!\r\n");
        cfg_ok = false;
    } else {
        LORA_LOG("[DRV] Configured Module UART to Param: %d (%d bps)\r\n", 
                 baud_param, LORA_TARGET_BAUDRATE);
    }
    
    // 6. 退出配置模式 (MD0=0)
    Port_SetMD0(false);
    LORA_LOG("[DRV] Exiting Config Mode...\r\n");
    
    // 7. 等待模块重启生效
    // 模块在退出配置模式后会重启，AUX 会拉高一段时间
    OSAL_DelayMs(100); 
    uint32_t wait_start = OSAL_GetTick();
    // 等待 AUX 变高 (重启开始)
    while(!Port_GetAUX_Raw()) {
         if (OSAL_GetTick() - wait_start > 500) break; // 防止本来就是高或者没变高
    }
    // 等待 AUX 变低 (重启结束)
    wait_start = OSAL_GetTick();
    while(Port_GetAUX_Raw()) {
        if (OSAL_GetTick() - wait_start > 2000) {
            LORA_LOG("[DRV] Warning: Wait AUX Low Timeout\r\n");
            break;
        }
    }
    
    // 8. 【关键步骤】将 MCU 串口切换回目标波特率 (例如 9600)
    // 此时模块已经重启完毕，正工作在 9600 下
    Port_ReInitUart(LORA_TARGET_BAUDRATE);
    OSAL_DelayMs(100); // 给一点时间让波特率稳定
    
    // 9. 最终状态同步
    Port_SyncAuxState();
    Port_ClearRxBuffer();
    
    if (cfg_ok) {
        LORA_LOG("[DRV] Init Success. Running at %d bps\r\n", LORA_TARGET_BAUDRATE);
    } else {
        LORA_LOG("[DRV] Init Finished with Errors.\r\n");
    }
    
    return cfg_ok;
}

/**
 * @brief  异步发送数据 (非阻塞)
 */
bool Drv_AsyncSend(const uint8_t *data, uint16_t len) {
    // 1. 检查 AUX 状态 (硬件流控)
    if (Port_IsAuxBusy()) {
        // 双重检查：防止中断漏触发导致的软件标志位虚假忙碌
        if (Port_GetAUX_Raw()) {
            return false; // 真的忙
        } else {
            // 软件说忙，硬件说闲 -> 自愈
            Port_SyncAuxState();
        }
    }
    
    // 2. 启动 DMA 发送
    Port_WriteData(data, len);
    return true;
}

/**
 * @brief  读取接收数据
 */
uint16_t Drv_Read(uint8_t *buf, uint16_t max_len) {
    return Port_ReadData(buf, max_len);
}

/**
 * @brief  查询驱动是否忙碌
 */
bool Drv_IsBusy(void) {
    return Port_IsAuxBusy();
}
