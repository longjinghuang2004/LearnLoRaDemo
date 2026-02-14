#include "lora_manager.h"
#include "lora_manager_fsm.h"
#include "lora_manager_buffer.h"
#include "lora_service.h"
#include <string.h>

// ============================================================
//                    内存管理策略：分离架构
// ============================================================

// 1. RX 专用工作区 (全局静态)
// 用于接收解包、校验，独占使用，无竞争风险。
#define RX_WORKSPACE_SIZE  MGR_RX_BUF_SIZE 
static uint8_t s_RxWorkspace[RX_WORKSPACE_SIZE];

static LoRa_OnRxData_t s_OnRx;

void LoRa_Manager_Init(LoRa_OnRxData_t on_rx) {
    s_OnRx = on_rx;
    LoRa_Manager_Buffer_Init();
    LoRa_Manager_FSM_Init();
}

void LoRa_Manager_Run(void) {
    // 1. 从 Port 拉取数据
    LoRa_Manager_Buffer_PullFromPort();
    
    // 2. 解析数据包
    LoRa_Packet_t pkt;
    const LoRa_Config_t *cfg = LoRa_Service_GetConfig();
    
    memset(&pkt, 0, sizeof(pkt));
    
    if (LoRa_Manager_Buffer_GetRxPacket(&pkt, cfg->net_id, cfg->group_id, 
                                        s_RxWorkspace, RX_WORKSPACE_SIZE)) {
        
        // 3. 交给 FSM 处理 (ACK 逻辑 + 去重)
        // 【修改】根据返回值决定是否回调
        bool valid_new_packet = LoRa_Manager_FSM_ProcessRxPacket(&pkt);
        
        // 4. 如果是有效新数据包，回调上层
        if (valid_new_packet && s_OnRx) {
            s_OnRx(pkt.Payload, pkt.PayloadLen, pkt.SourceID);
        }
    }
    
    // 5. 运行状态机 (物理层发送调度)
    // 注意：这里复用 s_RxWorkspace 作为 TX 的 Peek 缓冲区。
    // 这是安全的，因为 Run 函数是单线程顺序执行的，此时 RX 处理已完成，
    // s_RxWorkspace 处于空闲状态，可以借给 FSM 用作 DMA 发送的临时跳板。
    LoRa_Manager_FSM_Run(s_RxWorkspace, RX_WORKSPACE_SIZE);
}

bool LoRa_Manager_Send(const uint8_t *payload, uint16_t len, uint16_t target_id) {
    // 【关键变更】TX 使用栈内存
    // 目的：支持 RTOS 多任务并发调用 Send，利用任务栈隔离上下文。
    // 大小：最大负载 + 协议头开销 (约15字节) + 安全余量
    uint8_t tx_stack_buf[LORA_MAX_PAYLOAD_LEN + 32]; 
    
    // 调用 FSM Send，传入栈指针
    return LoRa_Manager_FSM_Send(payload, len, target_id, tx_stack_buf, sizeof(tx_stack_buf));
}

bool LoRa_Manager_IsBusy(void) {
    return LoRa_Manager_FSM_IsBusy();
}
