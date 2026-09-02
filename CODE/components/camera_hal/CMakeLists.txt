idf_component_register(
    SRCS "camera_hal.c" "camera_hal_policy.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES bus_i2c io_expander
    PRIV_REQUIRES esp_driver_gpio
)
