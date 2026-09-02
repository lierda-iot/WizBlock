idf_component_register(
    SRCS
        "audio_aec_test_main.c"
        "aec_test_logic.c"
    INCLUDE_DIRS "."
    REQUIRES
        audio_processor
        laiwfs300
        io_expander
)
