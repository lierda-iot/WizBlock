idf_component_register(
    SRCS "audio_aec_demo_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        audio_processor
        laiwfs300
        io_expander
)
