idf_component_register(
    SRCS "companion_ui.c"
    INCLUDE_DIRS "include"
    REQUIRES
        companion_expression
        companion_touch_gesture
        laiwfs300
        display_hal
        touch_hal
        esp_timer
        heap
        freertos
        log
        lvgl
)
