idf_component_register(
    SRCS "bus_spi.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_driver_gpio esp_driver_spi
)
