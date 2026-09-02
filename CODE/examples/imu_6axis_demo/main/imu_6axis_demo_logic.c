#include "imu_6axis_demo_logic.h"

#include <math.h>
#include <stddef.h>

#define IMU_DEMO_RAD_TO_DEG            57.2957795f
#define IMU_DEMO_MIN_DT_SECONDS        0.005f
#define IMU_DEMO_MAX_DT_SECONDS        0.200f
#define IMU_DEMO_TILT_THRESHOLD_DEG    8.0f
#define IMU_DEMO_ROTATE_THRESHOLD_DPS  45.0f
#define IMU_DEMO_SHAKE_THRESHOLD_G     0.25f
#define IMU_DEMO_GRAVITY_EPSILON       0.001f

static float clamp_dt(float dt_seconds)
{
    if (dt_seconds < IMU_DEMO_MIN_DT_SECONDS) {
        return IMU_DEMO_MIN_DT_SECONDS;
    }
    if (dt_seconds > IMU_DEMO_MAX_DT_SECONDS) {
        return IMU_DEMO_MAX_DT_SECONDS;
    }
    return dt_seconds;
}

static float wrap_degrees(float degrees)
{
    while (degrees > 180.0f) {
        degrees -= 360.0f;
    }
    while (degrees < -180.0f) {
        degrees += 360.0f;
    }
    return degrees;
}

static float vector_magnitude(float x, float y, float z)
{
    return sqrtf((x * x) + (y * y) + (z * z));
}

static imu_demo_motion_t classify_motion(const imu_demo_sample_t *sample)
{
    const float tilt = fmaxf(fabsf(sample->roll_deg), fabsf(sample->pitch_deg));
    const float dynamic_accel = fabsf(sample->accel_magnitude_g - 1.0f);

    if (sample->gyro_magnitude_dps >= IMU_DEMO_ROTATE_THRESHOLD_DPS) {
        return IMU_DEMO_MOTION_ROTATING;
    }
    if (dynamic_accel >= IMU_DEMO_SHAKE_THRESHOLD_G) {
        return IMU_DEMO_MOTION_SHAKING;
    }
    if (tilt >= IMU_DEMO_TILT_THRESHOLD_DEG) {
        return IMU_DEMO_MOTION_TILTED;
    }
    return IMU_DEMO_MOTION_STILL;
}

void imu_demo_filter_init(imu_demo_filter_t *filter)
{
    if (NULL == filter) {
        return;
    }

    *filter = (imu_demo_filter_t){0};
}

void imu_demo_filter_set_gyro_bias(imu_demo_filter_t *filter, imu_demo_raw_vector_t bias)
{
    if (NULL == filter) {
        return;
    }

    filter->gyro_bias_x_raw = (float)bias.x;
    filter->gyro_bias_y_raw = (float)bias.y;
    filter->gyro_bias_z_raw = (float)bias.z;
}

void imu_demo_filter_reset_attitude(imu_demo_filter_t *filter,
                                    const imu_demo_raw_vector_t *accel)
{
    float accel_x = 0.0f;
    float accel_y = 0.0f;
    float accel_z = 0.0f;
    float horizontal = 0.0f;

    if (NULL == filter || NULL == accel) {
        return;
    }

    accel_x = (float)accel->x / IMU_DEMO_ACCEL_LSB_PER_G;
    accel_y = (float)accel->y / IMU_DEMO_ACCEL_LSB_PER_G;
    accel_z = (float)accel->z / IMU_DEMO_ACCEL_LSB_PER_G;
    horizontal = hypotf(accel_y, accel_z);
    filter->roll_deg = atan2f(accel_y, accel_z) * IMU_DEMO_RAD_TO_DEG;
    filter->pitch_deg = atan2f(-accel_x, horizontal) * IMU_DEMO_RAD_TO_DEG;
    filter->yaw_deg = 0.0f;
    filter->initialized = true;
}

void imu_demo_filter_update(imu_demo_filter_t *filter,
                            imu_demo_raw_vector_t accel,
                            imu_demo_raw_vector_t gyro,
                            float dt_seconds,
                            imu_demo_sample_t *sample)
{
    float accel_roll = 0.0f;
    float accel_pitch = 0.0f;
    float horizontal = 0.0f;
    float gyro_x_dps = 0.0f;
    float gyro_y_dps = 0.0f;
    float gyro_z_dps = 0.0f;
    float integrated_roll = 0.0f;
    float integrated_pitch = 0.0f;

    if (NULL == filter || NULL == sample) {
        return;
    }

    sample->accel_x_g = (float)accel.x / IMU_DEMO_ACCEL_LSB_PER_G;
    sample->accel_y_g = (float)accel.y / IMU_DEMO_ACCEL_LSB_PER_G;
    sample->accel_z_g = (float)accel.z / IMU_DEMO_ACCEL_LSB_PER_G;
    gyro_x_dps = ((float)gyro.x - filter->gyro_bias_x_raw) / IMU_DEMO_GYRO_LSB_PER_DPS;
    gyro_y_dps = ((float)gyro.y - filter->gyro_bias_y_raw) / IMU_DEMO_GYRO_LSB_PER_DPS;
    gyro_z_dps = ((float)gyro.z - filter->gyro_bias_z_raw) / IMU_DEMO_GYRO_LSB_PER_DPS;
    sample->gyro_x_dps = gyro_x_dps;
    sample->gyro_y_dps = gyro_y_dps;
    sample->gyro_z_dps = gyro_z_dps;
    sample->accel_magnitude_g = vector_magnitude(sample->accel_x_g,
                                                 sample->accel_y_g,
                                                 sample->accel_z_g);
    sample->gyro_magnitude_dps = vector_magnitude(gyro_x_dps, gyro_y_dps, gyro_z_dps);

    horizontal = hypotf(sample->accel_y_g, sample->accel_z_g);
    if (sample->accel_magnitude_g > IMU_DEMO_GRAVITY_EPSILON) {
        accel_roll = atan2f(sample->accel_y_g, sample->accel_z_g) * IMU_DEMO_RAD_TO_DEG;
        accel_pitch = atan2f(-sample->accel_x_g, horizontal) * IMU_DEMO_RAD_TO_DEG;
    }

    if (!filter->initialized) {
        imu_demo_filter_reset_attitude(filter, &accel);
    }

    dt_seconds = clamp_dt(dt_seconds);
    integrated_roll = filter->roll_deg + (gyro_x_dps * dt_seconds);
    integrated_pitch = filter->pitch_deg + (gyro_y_dps * dt_seconds);
    filter->roll_deg = (IMU_DEMO_COMPLEMENTARY_ALPHA * integrated_roll) +
                       ((1.0f - IMU_DEMO_COMPLEMENTARY_ALPHA) * accel_roll);
    filter->pitch_deg = (IMU_DEMO_COMPLEMENTARY_ALPHA * integrated_pitch) +
                        ((1.0f - IMU_DEMO_COMPLEMENTARY_ALPHA) * accel_pitch);
    filter->yaw_deg = wrap_degrees(filter->yaw_deg + (gyro_z_dps * dt_seconds));

    sample->roll_deg = filter->roll_deg;
    sample->pitch_deg = filter->pitch_deg;
    sample->yaw_deg = filter->yaw_deg;
    sample->motion = classify_motion(sample);
}

const char *imu_demo_motion_text(imu_demo_motion_t motion)
{
    switch (motion) {
    case IMU_DEMO_MOTION_TILTED:
        return "TILTED";
    case IMU_DEMO_MOTION_ROTATING:
        return "ROTATING";
    case IMU_DEMO_MOTION_SHAKING:
        return "SHAKING";
    case IMU_DEMO_MOTION_STILL:
    default:
        return "STILL";
    }
}
