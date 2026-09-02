idf_component_register(
    SRCS "audio_spatial_spectrum_demo_main.c"
         "audio_spatial_spectrum_doa_filter.c"
         "audio_spatial_spectrum_math.c"
         "audio_spatial_spectrum_processor.c"
         "audio_spatial_spectrum_ui.c"
    INCLUDE_DIRS "."
    REQUIRES
        launcher_return
        laiwfs300
        io_expander
        esp-sr
        esp-dsp
        display_hal
        touch_hal
    PRIV_REQUIRES
        esp_timer
        lvgl
)
