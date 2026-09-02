#pragma once

#include <stdbool.h>
#include <stdint.h>

#define IMU_DEMO_ACCEL_LSB_PER_G       8192.0f
#define IMU_DEMO_GYRO_LSB_PER_DPS      16.4f
#define IMU_DEMO_COMPLEMENTARY_ALPHA   0.98f

typedef struct {
    int16_t x;
    int16_t y;
    int16_t z;
} imu_demo_raw_vector_t;

typedef enum {
    IMU_DEMO_MOTION_STILL = 0,
    IMU_DEMO_MOTION_TILTED,
    IMU_DEMO_MOTION_ROTATING,
    IMU_DEMO_MOTION_SHAKING,
} imu_demo_motion_t;

typedef struct {
    float accel_x_g;
    float accel_y_g;
    float accel_z_g;
    float gyro_x_dps;
    float gyro_y_dps;
    float gyro_z_dps;
    float accel_magnitude_g;
    float gyro_magnitude_dps;
    float roll_deg;
    float pitch_deg;
    float yaw_deg;
    imu_demo_motion_t motion;
} imu_demo_sample_t;

typedef struct {
    float gyro_bias_x_raw;
    float gyro_bias_y_raw;
    float gyro_bias_z_raw;
    float roll_deg;
    float pitch_deg;
    float yaw_deg;
    bool initialized;
} imu_demo_filter_t;

void imu_demo_filter_init(imu_demo_filter_t *filter);
void imu_demo_filter_set_gyro_bias(imu_demo_filter_t *filter, imu_demo_raw_vector_t bias);
void imu_demo_filter_reset_attitude(imu_demo_filter_t *filter,
                                    const imu_demo_raw_vector_t *accel);
void imu_demo_filter_update(imu_demo_filter_t *filter,
                            imu_demo_raw_vector_t accel,
                            imu_demo_raw_vector_t gyro,
                            float dt_seconds,
                            imu_demo_sample_t *sample);

const char *imu_demo_motion_text(imu_demo_motion_t motion);
