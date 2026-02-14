#include "lora_manager.h"
#include "lora_manager_fsm.h"
#include "lora_manager_buffer.h"
#include "lora_service.h" // 获取 Config
#include <string.h>

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
    
    // 这里的 memset 很重要，防止脏数据
    memset(&pkt, 0, sizeof(pkt));
    
    if (LoRa_Manager_Buffer_GetRxPacket(&pkt, cfg->net_id, cfg->group_id)) {
        // 3. 交给 FSM 处理 (ACK 逻辑)
        LoRa_Manager_FSM_ProcessRxPacket(&pkt);
        
        // 4. 如果是数据包，回调上层
        if (!pkt.IsAckPacket && s_OnRx) {
            s_OnRx(pkt.Payload, pkt.PayloadLen, pkt.SourceID);
        }
    }
    
    // 5. 运行状态机
    LoRa_Manager_FSM_Run();
}

bool LoRa_Manager_Send(const uint8_t *payload, uint16_t len, uint16_t target_id) {
    return LoRa_Manager_FSM_Send(payload, len, target_id);
}

bool LoRa_Manager_IsBusy(void) {
    return LoRa_Manager_FSM_IsBusy();
}
