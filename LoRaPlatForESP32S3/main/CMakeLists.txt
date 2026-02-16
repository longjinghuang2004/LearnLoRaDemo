idf_component_register(
    SRCS "main.c"
    INCLUDE_DIRS "."
    # 依赖我们自定义的组件和系统组件
    REQUIRES loraplat bsp_led bsp_button nvs_flash
)
