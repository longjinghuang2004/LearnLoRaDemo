#include "stm32f10x.h"
#include "Delay.h"
#include "LED.h"
#include "Serial.h"
#include "Timer.h"
#include "Model.h"
#include "Flash.h"
#include "InputParser.h"
#include "Algorithm.h"
#include "Indicator.h"
#include <string.h>

// --- 批次接收状态机定义 ---
typedef enum {
    STATE_IDLE,
    STATE_RECEIVING
} BatchState;

// --- 全局状态变量 ---
volatile BatchState g_batch_state = STATE_IDLE;
volatile uint8_t g_TimeoutFlag = 0;

char      g_current_batch_id[BATCH_ID_MAX_LEN];
uint8_t   g_expected_frames = 0;
uint32_t  g_received_frames_mask = 0;
uint8_t   g_received_frames_count = 0;

ForecastInput g_BatchBuffer;
static char processing_buffer[SERIAL_RX_BUFFER_SIZE];


// --- 前向声明 ---
static void Process_DataPacket(const char *json_str);
static void Process_ParamPacket(const char *json_str);
void Process_ReceivedPacket(const char *packet_buffer);
static void Handle_BatchTimeout(void);
static void Reset_BatchState(void);
static void Run_FlashCorruptionTest_ForNextBoot(void); // [新增] 测试函数的声明


/**
  * @brief  [新增] 封装Flash自检逻辑的测试函数
  * @note   此函数会故意污染Flash中的标志位，以测试下一次上电时
  *         系统是否能正确执行“恢复出厂设置”的流程。
  */
static void Run_FlashCorruptionTest_ForNextBoot(void)
{
    printf("\n[Test Logic] Invalidating Flash flag to 41 for next boot test.\n");
    g_ModelParams.written_flag = 41; // 在RAM中将标志位设置为无效值
    Flash_WriteModelParams(&g_ModelParams); // 将包含无效标志的结构体写回Flash
}


/**
  * @brief  主函数
  */
int main(void)
{
    // 1. 初始化所有硬件和模块
    LED_Init();
    Serial_Init();
    Model_Init();
    Timeout_Timer_Init();
    
    printf("\r\n--- System Boot ---\r\n");
    
    for (int i = 0; i < 4; i++) {
        LED1_Turn();
        Delay_ms(200);
    }
    LED1_OFF();
    
    // [修改] 调用封装好的测试函数。后续可直接注释掉此行来禁用该功能。
    Run_FlashCorruptionTest_ForNextBoot();
    
    printf("System Ready. Waiting for data or param packet...\r\n");

    // 2. 进入主循环
    while (1)
    {
        if (Serial_RxFlag == 1)
        {
            LED1_ON();
            
            USART_ITConfig(USART1, USART_IT_RXNE, DISABLE);
            strcpy(processing_buffer, Serial_RxPacket);
            Serial_RxFlag = 0;
            USART_ITConfig(USART1, USART_IT_RXNE, ENABLE);

            Process_ReceivedPacket(processing_buffer);
            
            LED1_OFF();
        }
        
        if (g_TimeoutFlag == 1)
        {
            Handle_BatchTimeout();
        }
    }
}

// ... Process_ReceivedPacket, Process_DataPacket, Process_ParamPacket, Handle_BatchTimeout, Reset_BatchState 等函数保持完全不变 ...

/**
  * @brief  协议分发器 (此函数无变化)
  */
void Process_ReceivedPacket(const char *packet_buffer)
{
    if (strncmp(packet_buffer, "param{", 6) == 0)
    {
        Process_ParamPacket(packet_buffer + 5);
    }
    else if (strncmp(packet_buffer, "data{", 5) == 0)
    {
        Process_DataPacket(packet_buffer + 4);
    }
    else
    {
        printf("\r\n--- Unknown Packet Type ---\r\n");
    }
}

/**
  * @brief  处理 "data" 类型的分包JSON (此函数无变化)
  */
static void Process_DataPacket(const char *json_str)
{
    BatchFrame frame;
    
    if (!Parse_BatchFrame(json_str, &frame))
    {
        printf("Packet parsing failed. Ignoring.\r\n");
        return;
    }
    
    printf("Received Frame: BatchID=%s, Index=%d, Total=%d\r\n", frame.batch_id, frame.frame_index, frame.total_frames);

    if (g_batch_state == STATE_IDLE)
    {
        if (frame.frame_index == 0)
        {
            printf("New batch started. State -> RECEIVING.\r\n");
            g_batch_state = STATE_RECEIVING;
            
            strncpy(g_current_batch_id, frame.batch_id, BATCH_ID_MAX_LEN - 1);
            g_expected_frames = frame.total_frames;
            g_received_frames_mask = 0;
            g_received_frames_count = 0;
            
            if (g_expected_frames > MAX_FRAMES_PER_BATCH) {
                printf("Error: Batch total_frames (%d) exceeds buffer size (%d).\r\n", g_expected_frames, MAX_FRAMES_PER_BATCH);
                Reset_BatchState();
                return;
            }
            
            memset(&g_BatchBuffer, 0, sizeof(g_BatchBuffer));
            strncpy(g_BatchBuffer.station_id, frame.station_id, STATION_ID_MAX_LEN - 1);
            
            Timeout_Timer_Start();
        }
        else
        {
            printf("Warning: Received non-start frame in IDLE state. Ignoring.\r\n");
            return;
        }
    }
    else
    {
        if (strcmp(g_current_batch_id, frame.batch_id) != 0)
        {
            printf("Warning: Received frame from a different batch. Ignoring.\r\n");
            return;
        }
        Timeout_Timer_Reset();
    }
    
    if (frame.frame_index >= g_expected_frames)
    {
        printf("Error: Frame index out of bounds. Ignoring.\r\n");
        return;
    }
    
    if ((g_received_frames_mask & (1 << frame.frame_index)) == 0)
    {
        g_BatchBuffer.time_series[frame.frame_index] = frame.payload;
        
        g_received_frames_mask |= (1 << frame.frame_index);
        g_received_frames_count++;
        
        printf("Frame %d stored. Progress: %d/%d\r\n", frame.frame_index, g_received_frames_count, g_expected_frames);
    }
    else
    {
        printf("Warning: Duplicate frame %d received. Ignoring.\r\n", frame.frame_index);
    }
    
    if (g_received_frames_count == g_expected_frames)
    {
        printf("\r\n--- Batch %s Complete! ---\r\n", g_current_batch_id);
        Timeout_Timer_Stop();
        
        g_BatchBuffer.time_series_count = g_expected_frames;
        RiskResult results[MAX_FRAMES_PER_BATCH];

        Algorithm_CalculateAllRisks(&g_BatchBuffer, &g_ModelParams, results);
        printf("Risk calculation complete.\r\n");

        Indicator_ShowRiskResults(&g_BatchBuffer, results);
        
        Reset_BatchState();
    }
}

/**
  * @brief  处理 "param" 包
  * @note   [优化] 更新成功后，打印当前所有模型参数的完整快照
  */
static void Process_ParamPacket(const char *json_str)
{
    printf("\r\n--- Param Packet Detected ---\r\n");
    
    // 1. 解析并更新参数
    int updated_count = Parse_ParamInput(json_str, &g_ModelParams);
    
    // 2. 如果有参数被更新
    if (updated_count > 0)
    {
        printf("Successfully updated %d parameter(s).\r\n", updated_count);
        
        // 3. 写入Flash
        printf("Writing updated parameters to Flash...\r\n");
        Flash_WriteModelParams(&g_ModelParams);
        printf("Write complete. Changes will be effective after reset.\r\n");
        
        // 4. [新增] 打印当前所有模型参数的完整快照
        printf("\r\n=== Current Model Parameters Snapshot ===\r\n");
        printf("  [Device Info]\r\n");
        printf("    station_id:                %.2f\r\n", g_ModelParams.station_id);
        printf("    location:                  %.2f\r\n", g_ModelParams.location);
        
        printf("  [Base Risks]\r\n");
        printf("    overflow_base:             %.3f\r\n", g_ModelParams.overflow_base);
        printf("    instability_base:          %.3f\r\n", g_ModelParams.instability_base);
        
        printf("  [Overflow Weights]\r\n");
        printf("    w_overflow_wave_height:    %.3f\r\n", g_ModelParams.w_overflow_wave_height);
        printf("    w_overflow_water_level:    %.3f\r\n", g_ModelParams.w_overflow_water_level);
        
        printf("  [Instability Weights]\r\n");
        printf("    w_instability_wave_height: %.3f\r\n", g_ModelParams.w_instability_wave_height);
        printf("    w_instability_water_level: %.3f\r\n", g_ModelParams.w_instability_water_level);
        
        printf("  [Breach Weights]\r\n");
        printf("    w_breach_overflow:         %.3f\r\n", g_ModelParams.w_breach_overflow);
        printf("    w_breach_instability:      %.3f\r\n", g_ModelParams.w_breach_instability);
        
        printf("  [Total Weights]\r\n");
        printf("    w_total_overflow:          %.3f\r\n", g_ModelParams.w_total_overflow);
        printf("    w_total_breach:            %.3f\r\n", g_ModelParams.w_total_breach);
        
        printf("  [Normalization Params]\r\n");
        printf("    norm_wave_height_L/U:      %.2f / %.2f\r\n", g_ModelParams.norm_wave_height_L, g_ModelParams.norm_wave_height_U);
        printf("    norm_water_level_L/U:      %.2f / %.2f\r\n", g_ModelParams.norm_water_level_L, g_ModelParams.norm_water_level_U);
        
        printf("  [Thresholds]\r\n");
        printf("    Low/Medium/High:           %.2f / %.2f / %.2f\r\n", g_ModelParams.threshold_low, g_ModelParams.threshold_medium, g_ModelParams.threshold_high);
        printf("=========================================\r\n");
    }
    else
    {
        printf("--- Param Packet Parsing Failed or No valid parameters found ---\r\n");
    }
}

/**
  * @brief  处理数据包接收超时事件 (此函数无变化)
  */
static void Handle_BatchTimeout(void)
{
    g_TimeoutFlag = 0;
    if (g_batch_state == STATE_RECEIVING)
    {
        printf("\r\n--- ERROR: Batch Reception Timeout! ---\r\n");
        printf("Batch ID: %s\r\n", g_current_batch_id);
        printf("Expected %d frames, but only received %d.\r\n", g_expected_frames, g_received_frames_count);
        
        Reset_BatchState();
    }
}

/**
  * @brief  重置批次状态机到初始状态 (此函数无变化)
  */
static void Reset_BatchState(void)
{
    g_batch_state = STATE_IDLE;
    g_current_batch_id[0] = '\0';
    g_expected_frames = 0;
    g_received_frames_mask = 0;
    g_received_frames_count = 0;
    printf("\r\nState machine reset to IDLE. Waiting for new batch...\r\n");
}
