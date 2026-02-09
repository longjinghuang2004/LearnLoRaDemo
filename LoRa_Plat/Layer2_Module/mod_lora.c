/**
  ******************************************************************************
  * @file    mod_lora.c
  * @author  None
  * @brief   Layer 2: LoRa 中间件实现
  ******************************************************************************
  */

#include "mod_lora.h"
#include <string.h>

/* ========================================================================== */
/*                                 内部辅助函数                                */
/* ========================================================================== */

// 等待 AUX 引脚变低 (空闲)
static void _LoRa_WaitAux(LoRa_Dev_t *dev)
{
    if (dev->drv.ReadAUX && dev->drv.DelayMs)
    {
        // 简单的超时保护，防止死锁
        uint32_t timeout = 1000; 
        while (dev->drv.ReadAUX() == 1 && timeout > 0)
        {
            dev->drv.DelayMs(2); // 稍微延时
            timeout--;
        }
    }
}

/* ========================================================================== */
/*                                 接口实现                                   */
/* ========================================================================== */

/**
  * @brief  1. 初始化 LoRa 中间件
  */
bool LoRa_Init(LoRa_Dev_t *dev, const LoRa_Driver_t *driver)
{
    if (dev == NULL || driver == NULL) return false;
    
    // 1. 绑定驱动接口
    dev->drv = *driver;
    
    // 2. 初始化内部状态
    memset(dev->rx_buf, 0, LORA_INTERNAL_RX_BUF_SIZE);
    dev->rx_index = 0;
    dev->is_receiving_packet = false;
    
    // 3. 设置默认包头包尾
    dev->head[0] = LORA_DEFAULT_HEAD_0;
    dev->head[1] = LORA_DEFAULT_HEAD_1;
    dev->tail[0] = LORA_DEFAULT_TAIL_0;
    dev->tail[1] = LORA_DEFAULT_TAIL_1;
    
    // 4. 硬件复位流程 (可选，依赖 SetMD0)
    if (dev->drv.SetMD0)
    {
        dev->drv.SetMD0(0); // 默认为通信模式
        if (dev->drv.DelayMs) dev->drv.DelayMs(100);
    }
    
    return true;
}

/**
  * @brief  2. 发送 AT 指令并等待响应
  */
bool LoRa_SendAT(LoRa_Dev_t *dev, const char *cmd, char *resp_buf, uint16_t resp_max_len, const char *expect_str, uint32_t timeout_ms)
{
    if (dev == NULL || cmd == NULL) return false;
    
    // 1. 进入配置模式 (MD0 = 1)
    if (dev->drv.SetMD0)
    {
        dev->drv.SetMD0(1);
        if (dev->drv.DelayMs) dev->drv.DelayMs(50);
        _LoRa_WaitAux(dev);
    }
    
    // 2. 清空底层接收缓冲区 (防止残留数据干扰)
    //    注意：这里我们通过读取所有数据来清空
    uint8_t dummy;
    while (dev->drv.Read(&dummy, 1) > 0);
    
    // 3. 发送指令
    dev->drv.Send((const uint8_t*)cmd, strlen(cmd));
    
    // 4. 循环接收并等待响应
    uint32_t start_tick = dev->drv.GetTick();
    uint16_t rx_cnt = 0;
    bool found = false;
    
    // 如果用户没提供缓冲区，使用内部临时缓冲区
    char temp_buf[128];
    char *p_buf = (resp_buf != NULL) ? resp_buf : temp_buf;
    uint16_t max_len = (resp_buf != NULL) ? resp_max_len : sizeof(temp_buf);
    
    memset(p_buf, 0, max_len);
    
    while ((dev->drv.GetTick() - start_tick) < timeout_ms)
    {
        // 尝试读取一个字节
        if (dev->drv.Read((uint8_t*)&p_buf[rx_cnt], 1) > 0)
        {
            rx_cnt++;
            
            // 检查是否溢出
            if (rx_cnt >= max_len - 1)
            {
                p_buf[max_len - 1] = '\0'; // 强制结束
                break; 
            }
            
            // 检查是否包含期望字符串
            if (expect_str != NULL)
            {
                p_buf[rx_cnt] = '\0'; // 临时添加结束符以便 strstr
                if (strstr(p_buf, expect_str) != NULL)
                {
                    found = true;
                    break; // 找到了，提前退出
                }
            }
        }
    }
    
    // 5. 恢复通信模式 (MD0 = 0)
    if (dev->drv.SetMD0)
    {
        dev->drv.SetMD0(0);
        if (dev->drv.DelayMs) dev->drv.DelayMs(50);
        _LoRa_WaitAux(dev);
    }
    
    // 如果不需要匹配字符串，只要没超时就算成功
    if (expect_str == NULL) return true;
    
    return found;
}

/**
  * @brief  3. 单字节发送 (透传)
  */
void LoRa_SendByteRaw(LoRa_Dev_t *dev, uint8_t byte)
{
    if (dev && dev->drv.Send)
    {
        _LoRa_WaitAux(dev); // 发送前检查忙闲
        dev->drv.Send(&byte, 1);
    }
}

/**
  * @brief  4. 字符串/数据发送 (透传)
  */
void LoRa_SendDataRaw(LoRa_Dev_t *dev, const uint8_t *data, uint16_t len)
{
    if (dev && dev->drv.Send && data && len > 0)
    {
        _LoRa_WaitAux(dev);
        dev->drv.Send(data, len);
    }
}

/**
  * @brief  4.1 发送数据包 (自动添加包头包尾)
  */
void LoRa_SendPacket(LoRa_Dev_t *dev, const uint8_t *data, uint16_t len)
{
    if (dev && dev->drv.Send && data && len > 0)
    {
        _LoRa_WaitAux(dev);
        
        // 1. 发送包头
        dev->drv.Send(dev->head, 2);
        
        // 2. 发送数据
        dev->drv.Send(data, len);
        
        // 3. 发送包尾
        dev->drv.Send(dev->tail, 2);
    }
}

/**
  * @brief  5. 接收数据包 (轮询调用)
  */
uint16_t LoRa_ReceivePacket(LoRa_Dev_t *dev, uint8_t *out_buf, uint16_t max_len)
{
    if (dev == NULL || out_buf == NULL || max_len == 0) return 0;
    
    uint8_t byte;
    uint16_t payload_len = 0;
    
    // 1. 从底层驱动尽可能多地拉取数据
    //    注意：这里每次只处理一个字节，以便精确控制状态机
    while (dev->drv.Read(&byte, 1) > 0)
    {
        // --- 状态机逻辑 ---
        
        // 放入内部缓冲区
        if (dev->rx_index < LORA_INTERNAL_RX_BUF_SIZE)
        {
            dev->rx_buf[dev->rx_index++] = byte;
        }
        else
        {
            // 缓冲区溢出，重置 (丢弃旧数据)
            dev->rx_index = 0;
            dev->is_receiving_packet = false;
            // 将当前字节作为新数据的开始尝试
            dev->rx_buf[dev->rx_index++] = byte;
        }
        
        // 检查是否正在接收包
        if (!dev->is_receiving_packet)
        {
            // 检查是否匹配包头 (至少要有2个字节)
            if (dev->rx_index >= 2)
            {
                uint16_t idx = dev->rx_index;
                if (dev->rx_buf[idx-2] == dev->head[0] && 
                    dev->rx_buf[idx-1] == dev->head[1])
                {
                    // 找到包头！
                    dev->is_receiving_packet = true;
                    
                    // 调整缓冲区：将包头移到最前面 (其实不需要移，只需标记开始)
                    // 为了简化逻辑，我们重置 index 为 0，
                    // 意味着 rx_buf[0] 将是 Payload 的第一个字节
                    dev->rx_index = 0; 
                }
            }
        }
        else
        {
            // 正在接收包，检查是否匹配包尾
            // 此时 rx_buf 里存放的是 Payload
            if (dev->rx_index >= 2)
            {
                uint16_t idx = dev->rx_index;
                if (dev->rx_buf[idx-2] == dev->tail[0] && 
                    dev->rx_buf[idx-1] == dev->tail[1])
                {
                    // 找到包尾！一包接收完成
                    
                    // 计算 Payload 长度 (当前索引 - 包尾2字节)
                    payload_len = dev->rx_index - 2;
                    
                    // 保护：如果 Payload 太长超过用户缓冲区
                    if (payload_len > max_len) payload_len = max_len;
                    
                    // 拷贝 Payload 到用户缓冲区
                    memcpy(out_buf, dev->rx_buf, payload_len);
                    
                    // 重置状态，准备接收下一包
                    dev->rx_index = 0;
                    dev->is_receiving_packet = false;
                    
                    return payload_len; // 返回长度，退出函数
                }
            }
            
            // 额外保护：如果收到新的包头 (包头重入)，说明上一包残缺
            // 比如收到: CM...Data...CM
            if (dev->rx_index >= 2)
            {
                uint16_t idx = dev->rx_index;
                if (dev->rx_buf[idx-2] == dev->head[0] && 
                    dev->rx_buf[idx-1] == dev->head[1])
                {
                    // 丢弃之前的残缺数据，重新开始
                    dev->rx_index = 0;
                    // 状态依然是 true (正在接收新包)
                }
            }
        }
    }
    
    return 0; // 本次轮询未收到完整包
}

/**
  * @brief  6. 设置包头
  */
void LoRa_SetPacketHeader(LoRa_Dev_t *dev, uint8_t h0, uint8_t h1)
{
    if (dev)
    {
        dev->head[0] = h0;
        dev->head[1] = h1;
    }
}

/**
  * @brief  7. 设置包尾
  */
void LoRa_SetPacketTail(LoRa_Dev_t *dev, uint8_t t0, uint8_t t1)
{
    if (dev)
    {
        dev->tail[0] = t0;
        dev->tail[1] = t1;
    }
}

/**
  * @brief  辅助: 驱动 LoRa 状态机
  */
void LoRa_Process(LoRa_Dev_t *dev)
{
    // 目前版本不需要后台任务，所有逻辑都在 ReceivePacket 和 Send 中处理
    // 预留此接口方便未来扩展 (如处理 ACK 重传)
    (void)dev;
}
