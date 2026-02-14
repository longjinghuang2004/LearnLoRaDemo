#include "lora_manager.h"
#include "lora_manager_fsm.h"
#include "lora_manager_buffer.h"
#include "lora_service.h"
#include <string.h>

// ============================================================
//                    内存优化：共享工作区
// ============================================================
// 定义一个足够大的共享缓冲区，供所有子模块轮流使用
// 大小取 Max(MGR_TX_BUF_SIZE, MGR_RX_BUF_SIZE)
#define SHARED_WORKSPACE_SIZE  512 
static uint8_t s_SharedWorkspace[SHARED_WORKSPACE_SIZE];

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
    
    // 清空结构体，防止脏数据
    memset(&pkt, 0, sizeof(pkt));
    
    // 【优化】传入共享缓冲区
    if (LoRa_Manager_Buffer_GetRxPacket(&pkt, cfg->net_id, cfg->group_id, 
                                        s_SharedWorkspace, SHARED_WORKSPACE_SIZE)) {
        // 3. 交给 FSM 处理 (ACK 逻辑)
        LoRa_Manager_FSM_ProcessRxPacket(&pkt);
        
        // 4. 如果是数据包，回调上层
        if (!pkt.IsAckPacket && s_OnRx) {
            s_OnRx(pkt.Payload, pkt.PayloadLen, pkt.SourceID);
        }
    }
    
    // 5. 运行状态机
    // 【优化】传入共享缓冲区用于 PeekTx
    LoRa_Manager_FSM_Run(s_SharedWorkspace, SHARED_WORKSPACE_SIZE);
}

bool LoRa_Manager_Send(const uint8_t *payload, uint16_t len, uint16_t target_id) {
    // 【优化】Send 也是瞬时操作，可以使用共享缓冲区
    return LoRa_Manager_FSM_Send(payload, len, target_id, s_SharedWorkspace, SHARED_WORKSPACE_SIZE);
}

bool LoRa_Manager_IsBusy(void) {
    return LoRa_Manager_FSM_IsBusy();
}
