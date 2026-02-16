# 确保这一行里的每一个文件，都在你的硬盘上真实存在！
idf_component_register(
    SRCS 
        "src/0_OSAL/lora_osal.c"        # <--- 必须有！
        "src/0_OSAL/lora_osal_esp32.c"  # <--- 必须有！
        "src/0_Utils/lora_crc16.c"
        "src/0_Utils/lora_ring_buffer.c"
        "src/1_Port/lora_port_esp32.c"
        "src/2_Driver/lora_driver.c"
        "src/2_Driver/lora_driver_core.c"
        "src/2_Driver/lora_driver_config.c"
        "src/2_Driver/lora_at_command_engine.c"
        "src/3_Manager/lora_manager.c"
        "src/3_Manager/lora_manager_buffer.c"
        "src/3_Manager/lora_manager_fsm.c"
        "src/3_Manager/lora_manager_protocol.c"
        "src/4_Service/lora_service.c"
        "src/4_Service/lora_service_config.c"
        "src/4_Service/lora_service_command.c"
        "src/4_Service/lora_service_monitor.c"

    INCLUDE_DIRS 
        "src"
        "src/0_OSAL"
        "src/0_Utils"
        "src/1_Port"
        "src/2_Driver"
        "src/3_Manager"
        "src/4_Service"

    REQUIRES 
        driver
        esp_timer
        log
        nvs_flash # 如果你在代码里用了 nvs，最好加上
)
