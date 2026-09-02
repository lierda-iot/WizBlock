idf_component_register(
    SRCS "companion_motion.c" "companion_motion_result_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES bmi260_imu companion_logic companion_turn_control laiwfs300 robot_motion esp_timer freertos log
)
