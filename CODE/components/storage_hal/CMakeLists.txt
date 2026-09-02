idf_component_register(
    SRCS "storage_hal.c"
    INCLUDE_DIRS "include"
    REQUIRES fatfs esp_driver_sdspi sdmmc
)
