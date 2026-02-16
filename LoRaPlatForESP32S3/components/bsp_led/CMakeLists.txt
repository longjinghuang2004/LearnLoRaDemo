idf_component_register(
    SRCS "src/bsp_led.c"
    INCLUDE_DIRS "include"
    REQUIRES led_strip  # 依赖官方组件
)
