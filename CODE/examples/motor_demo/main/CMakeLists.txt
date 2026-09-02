idf_component_register(
    SRCS "motor_demo_main.c"
    INCLUDE_DIRS "."
    REQUIRES laiwfs300 robot_motion pt2466_motor
)
