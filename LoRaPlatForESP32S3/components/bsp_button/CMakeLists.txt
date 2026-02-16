# components/bsp_button/CMakeLists.txt

idf_component_register(
    SRCS "src/Key.c" "src/KeyManager.c" "src/key_port_esp32.c"
    INCLUDE_DIRS "include"
    REQUIRES driver esp_timer  # <--- 在这里添加 esp_timer
)
