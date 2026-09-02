idf_component_register(
    SRCS
        "audio_ns_demo_main.c"
        "audio_ns_demo_logic.c"
        "audio_ns_demo_ui.c"
    INCLUDE_DIRS "."
    REQUIRES
        audio_processor
        display_hal
        launcher_return
        laiwfs300
        io_expander
        touch_hal
    PRIV_REQUIRES
        esp_timer
        lvgl
)
