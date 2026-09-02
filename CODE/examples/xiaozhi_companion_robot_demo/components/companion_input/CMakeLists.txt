idf_component_register(
    SRCS "companion_input.c"
    INCLUDE_DIRS "include"
    REQUIRES board_adc esp_adc freertos log
)
