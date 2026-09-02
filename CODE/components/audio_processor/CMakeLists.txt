idf_component_register(
    SRCS
        "audio_processor.c"
        "audio_processor_policy.c"
        "audio_processor_task_policy.c"
        "opus_codec.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp-sr
        esp_audio_codec
    PRIV_REQUIRES
        esp_partition
        spiffs
)
