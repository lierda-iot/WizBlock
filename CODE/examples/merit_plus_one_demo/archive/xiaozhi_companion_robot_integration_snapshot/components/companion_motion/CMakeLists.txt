idf_component_register(
    SRCS "companion_motion.c"
    INCLUDE_DIRS "include"
    REQUIRES bmi260_imu companion_logic companion_turn_control companion_merit_tap laiwfs300 robot_motion esp_timer freertos log
)
