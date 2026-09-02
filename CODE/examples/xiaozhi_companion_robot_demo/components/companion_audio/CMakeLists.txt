idf_component_register(
    SRCS "companion_audio.c"
         "companion_audio_processor_policy.c"
         "companion_audio_pcm_queue.c"
         "companion_audio_vad_policy.c"
         "companion_audio_voice_gate.c"
    INCLUDE_DIRS "include"
    REQUIRES
        audio_processor
        laiwfs300
        io_expander
        freertos
        log
        heap
        esp_timer
        espressif__esp_audio_simple_player
)
