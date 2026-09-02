idf_component_register(
    SRCS "aip8563_rtc.c"
    INCLUDE_DIRS "include"
    REQUIRES bus_i2c laiwfs300
)
