idf_component_register(
    SRCS "robot_motion.c"
    INCLUDE_DIRS "include"
    REQUIRES pt2466_motor
    PRIV_REQUIRES freertos log
)
