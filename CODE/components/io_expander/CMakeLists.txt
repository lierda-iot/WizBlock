idf_component_register(
    SRCS "io_expander.c"
    INCLUDE_DIRS "include"
    REQUIRES
        bus_i2c
        esp_driver_i2c
)
