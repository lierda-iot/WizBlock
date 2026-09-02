idf_component_register(
    SRCS "bmi260_imu.c"
    INCLUDE_DIRS "include"
    REQUIRES bus_i2c laiwfs300
)
