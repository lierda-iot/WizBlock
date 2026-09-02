idf_component_register(
    SRCS
        "camera_face_detect_demo_main.cpp"
        "flash_param_face_detect.cpp"
    INCLUDE_DIRS "."
    REQUIRES
        laiwfs300
        camera_hal
        display_hal
        esp_driver_cam
        human_face_detect
        esp-dl
    PRIV_REQUIRES
        esp_timer
        driver
        esp_driver_ledc
)
