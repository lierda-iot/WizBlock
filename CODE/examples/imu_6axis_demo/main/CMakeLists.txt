idf_component_register(
    SRCS
        "imu_6axis_demo_main.c"
        "imu_6axis_demo_logic.c"
        "imu_6axis_demo_ui.c"
    INCLUDE_DIRS "."
    REQUIRES
        launcher_return
        laiwfs300
        bmi260_imu
        display_hal
        touch_hal
    PRIV_REQUIRES
        esp_timer
        lvgl
)
