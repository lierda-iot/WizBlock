idf_component_register(
    SRCS "companion_doa.c" "companion_doa_estimator.c"
    INCLUDE_DIRS "include"
    REQUIRES companion_audio esp-sr freertos heap log
)
