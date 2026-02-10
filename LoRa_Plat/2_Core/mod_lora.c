/**
  ******************************************************************************
  * @file    mod_lora.c
  * @author  LoRaPlat Team
  * @brief   Layer 2 Implementation - V2.1
  ******************************************************************************
  */

#include "mod_lora.h"
#include <string.h>
#include <stdio.h>

// [Debug Tool]
#include "Serial.h" 
#define LORA_DEBUG_ENABLE  1

#if LORA_DEBUG_ENABLE
    #define LORA_LOG(fmt, ...) Serial_Printf("[LoRa Core] " fmt "\r\n", ##__VA_ARGS__)
#else
    #define LORA_LOG(fmt, ...) ((void)0)
#endif

/* ========================================================================== */
/*                                 内部辅助函数                                */
/* ========================================================================== */

static void _WaitAuxIdle(LoRa_Dev_t *dev, uint32_t timeout_ms)
{
    if (!dev->io.GetAux || !dev->io.GetTick) return;
    
    uint32_t start = dev->io.GetTick();
    while (dev->io.GetAux() == 1)
    {
        if (dev->io.GetTick() - start > timeout_ms) {
            LORA_LOG("Error: AUX Wait Timeout!");
            break;
        }
        if (dev->io.DelayMs) dev->io.DelayMs(1);
    }
}

static void _ClearRxBuffer(LoRa_Dev_t *dev)
{
    uint8_t dummy;
    while (dev->io.Read(&dummy, 1) > 0);
}

/* ========================================================================== */
/*                                 核心 API 实现                               */
/* ========================================================================== */

bool LoRa_Init(LoRa_Dev_t *dev, const LoRa_IO_t *io_interface)
{
    if (!dev || !io_interface) return false;
    
    // 1. 绑定接口
    dev->io = *io_interface;
    
    // 2. 初始化状态
    dev->rx_callback = NULL;
    dev->rx_idx = 0;
    dev->rx_state = RX_STATE_WAIT_HEAD_0;
    dev->is_initialized = true;
    
    // 3. 初始化默认协议 (CM \r\n)
    dev->head[0] = LORA_DEFAULT_HEAD_0;
    dev->head[1] = LORA_DEFAULT_HEAD_1;
    dev->tail[0] = LORA_DEFAULT_TAIL_0;
    dev->tail[1] = LORA_DEFAULT_TAIL_1;
    
    // 4. 硬件复位
    if (dev->io.SetReset) {
        dev->io.SetReset(0);
        dev->io.DelayMs(100);
        dev->io.SetReset(1);
        dev->io.DelayMs(500);
    }
    
    // 5. 进入通信模式
    LoRa_EnterCommMode(dev);
    
    LORA_LOG("Initialized.");
    return true;
}

void LoRa_RegisterCallback(LoRa_Dev_t *dev, LoRa_RxHandler_t cb)
{
    if (dev) dev->rx_callback = cb;
}

void LoRa_SetPacketHeader(LoRa_Dev_t *dev, uint8_t h0, uint8_t h1)
{
    if (dev) {
        dev->head[0] = h0;
        dev->head[1] = h1;
        LORA_LOG("Header Set: %02X %02X", h0, h1);
    }
}

void LoRa_SetPacketTail(LoRa_Dev_t *dev, uint8_t t0, uint8_t t1)
{
    if (dev) {
        dev->tail[0] = t0;
        dev->tail[1] = t1;
        LORA_LOG("Tail Set: %02X %02X", t0, t1);
    }
}

void LoRa_Send(LoRa_Dev_t *dev, const uint8_t *data, uint16_t len)
{
    if (!dev || !dev->is_initialized || len == 0) return;
    
    _WaitAuxIdle(dev, 1000);
    
    uint8_t packet[LORA_MAX_PACKET_SIZE + 10];
    uint16_t idx = 0;
    
    // Header (动态读取)
    packet[idx++] = dev->head[0];
    packet[idx++] = dev->head[1];
    
    // Payload
    if (len > LORA_MAX_PACKET_SIZE) len = LORA_MAX_PACKET_SIZE;
    memcpy(&packet[idx], data, len);
    idx += len;
    
    // Tail (动态读取)
    packet[idx++] = dev->tail[0];
    packet[idx++] = dev->tail[1];
    
    dev->io.Write(packet, idx);
    LORA_LOG("Tx Packet (%d bytes)", idx);
}

void LoRa_Process(LoRa_Dev_t *dev)
{
    if (!dev || !dev->is_initialized) return;
    
    uint8_t chunk[32];
    uint16_t n_read = dev->io.Read(chunk, sizeof(chunk));
    if (n_read == 0) return;
    
    for (uint16_t i = 0; i < n_read; i++)
    {
        uint8_t byte = chunk[i];
        
        switch (dev->rx_state)
        {
            case RX_STATE_WAIT_HEAD_0:
                if (byte == dev->head[0]) { // 动态匹配
                    dev->rx_state = RX_STATE_WAIT_HEAD_1;
                }
                break;
                
            case RX_STATE_WAIT_HEAD_1:
                if (byte == dev->head[1]) { // 动态匹配
                    dev->rx_state = RX_STATE_RECEIVING;
                    dev->rx_idx = 0;
                } else if (byte != dev->head[0]) {
                    dev->rx_state = RX_STATE_WAIT_HEAD_0;
                    if (byte == dev->head[0]) dev->rx_state = RX_STATE_WAIT_HEAD_1;
                }
                break;
                
            case RX_STATE_RECEIVING:
                if (byte == dev->tail[0]) { // 动态匹配
                    dev->rx_state = RX_STATE_WAIT_TAIL_1;
                } else {
                    if (dev->rx_idx < LORA_MAX_PACKET_SIZE) {
                        dev->rx_buf[dev->rx_idx++] = byte;
                    } else {
                        LORA_LOG("Rx Overflow, Drop.");
                        dev->rx_state = RX_STATE_WAIT_HEAD_0;
                    }
                }
                break;
                
            case RX_STATE_WAIT_TAIL_1:
                if (byte == dev->tail[1]) { // 动态匹配
                    if (dev->rx_callback) {
                        if (dev->rx_idx < LORA_MAX_PACKET_SIZE) dev->rx_buf[dev->rx_idx] = '\0';
                        LORA_LOG("Rx Valid Packet (%d bytes)", dev->rx_idx);
                        dev->rx_callback(dev->rx_buf, dev->rx_idx);
                    }
                    dev->rx_state = RX_STATE_WAIT_HEAD_0;
                } else {
                    dev->rx_state = RX_STATE_WAIT_HEAD_0;
                }
                break;
        }
    }
}

/* ========================================================================== */
/*                                 配置 API 实现                               */
/* ========================================================================== */

void LoRa_EnterConfigMode(LoRa_Dev_t *dev)
{
    if (dev->io.SetModePin) {
        dev->io.SetModePin(1);
        dev->io.DelayMs(50);
    }
}

void LoRa_EnterCommMode(LoRa_Dev_t *dev)
{
    if (dev->io.SetModePin) {
        dev->io.SetModePin(0);
        dev->io.DelayMs(50);
        _WaitAuxIdle(dev, 1000);
    }
}

bool LoRa_SendAT(LoRa_Dev_t *dev, const char *cmd, const char *expect_resp, uint32_t timeout)
{
    if (!dev) return false;
    
    bool success = false;
    char resp_buf[64] = {0};
    uint16_t resp_len = 0;
    uint32_t start;
    
    LoRa_EnterConfigMode(dev);
    _ClearRxBuffer(dev);
    
    dev->io.Write((uint8_t*)cmd, strlen(cmd));
    LORA_LOG("AT Tx: %s", cmd);
    
    start = dev->io.GetTick();
    while (dev->io.GetTick() - start < timeout)
    {
        uint8_t byte;
        if (dev->io.Read(&byte, 1) > 0) {
            if (resp_len < sizeof(resp_buf) - 1) {
                resp_buf[resp_len++] = byte;
                resp_buf[resp_len] = '\0';
                if (expect_resp && strstr(resp_buf, expect_resp)) {
                    success = true;
                    break;
                }
            }
        }
    }
    
    if (success) LORA_LOG("AT Rx: OK");
    else LORA_LOG("AT Rx: Timeout");
    
    LoRa_EnterCommMode(dev);
    return success;
}

bool LoRa_SetChannel(LoRa_Dev_t *dev, uint8_t channel)
{
    if (channel > 31) return false;
    char cmd[32];
    sprintf(cmd, "AT+WLRATE=%d,5\r\n", channel);
    return LoRa_SendAT(dev, cmd, "OK", LORA_AT_TIMEOUT);
}

bool LoRa_SetPower(LoRa_Dev_t *dev, LoRa_Power_e power)
{
    char cmd[32];
    sprintf(cmd, "AT+TPOWER=%d\r\n", (int)power);
    return LoRa_SendAT(dev, cmd, "OK", LORA_AT_TIMEOUT);
}

bool LoRa_SetAirRate(LoRa_Dev_t *dev, LoRa_Rate_e rate)
{
    // 假设信道固定为23，只改速率
    char cmd[32];
    sprintf(cmd, "AT+WLRATE=23,%d\r\n", (int)rate);
    return LoRa_SendAT(dev, cmd, "OK", LORA_AT_TIMEOUT);
}
