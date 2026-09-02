idf_component_register(
    SRCS "audio_dual_mic_doa_demo_main.c"
         "audio_dual_mic_doa_filter.c"
         "audio_dual_mic_doa_ui.c"
    INCLUDE_DIRS "."
    REQUIRES
        launcher_return
        laiwfs300
        io_expander
        esp-sr
        display_hal
    PRIV_REQUIRES
        esp_timer
        lvgl
)
