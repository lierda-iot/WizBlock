idf_component_register(
    SRCS "xiaozhi_audio.c"
    INCLUDE_DIRS "include"
    REQUIRES
        audio_processor
        laiwfs300
        io_expander
        freertos
        log
        heap
        espressif__esp_audio_simple_player
)
