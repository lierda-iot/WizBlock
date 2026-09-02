idf_component_register(
    SRCS "board_laiwfs300.c"
    INCLUDE_DIRS "include"
    REQUIRES
        board_adc
        board_power
        bus_i2c
        bus_spi
        capability_registry
        io_expander
        module_detect
)
