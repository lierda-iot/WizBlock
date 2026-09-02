idf_component_register(
    SRCS "touch_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES bus_i2c io_expander laiwfs300
)
