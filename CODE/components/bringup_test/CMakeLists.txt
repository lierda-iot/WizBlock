idf_component_register(
    SRCS "bringup_test.c"
    INCLUDE_DIRS "include"
    REQUIRES
        aip8563_rtc
        audio_processor
        bmi260_imu
        camera_hal
        display_hal
        io_expander
        laiwfs300
        lte_hal
        net_mgmt
        pt2466_motor
        robot_motion
        touch_hal
    PRIV_REQUIRES
        esp_timer
        esp_wifi
        nvs_flash
)
