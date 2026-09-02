idf_component_register(
    SRCS "main.c"
         "hotplug_adc_model.c"
         "hotplug_manager.c"
         "hotplug_motor_runtime.c"
         "slot_expansion_c0.c"
         "slot_motor_d0.c"
         "slot_lcd.c"
         "slot_camera.c"
         "app_coordinator.c"
    INCLUDE_DIRS "."
    REQUIRES laiwfs300 robot_motion pt2466_motor camera_hal display_hal bus_i2c board_adc esp_driver_cam
    PRIV_REQUIRES esp_adc driver esp_timer
)
