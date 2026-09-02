idf_component_register(
    SRCS "camera_display_demo_main.c"
    INCLUDE_DIRS "."
    REQUIRES laiwfs300 camera_hal display_hal esp_driver_cam
    PRIV_REQUIRES esp_timer driver
)
