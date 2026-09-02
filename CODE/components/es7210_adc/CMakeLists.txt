idf_component_register(
    SRCS "es7210_adc.c"
    INCLUDE_DIRS "include"
    REQUIRES bus_i2c laiwfs300
)
