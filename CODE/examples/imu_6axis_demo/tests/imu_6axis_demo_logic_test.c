#include "imu_6axis_demo_logic.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>

#define TEST_EPSILON 0.1f

static void assert_near(float expected, float actual)
{
    assert(fabsf(expected - actual) < TEST_EPSILON);
}

static void test_level_accel_is_one_g(void)
{
    imu_demo_filter_t filter = {0};
    imu_demo_sample_t sample = {0};

    imu_demo_filter_init(&filter);
    imu_demo_filter_update(&filter,
                           (imu_demo_raw_vector_t){0, 0, 8192},
                           (imu_demo_raw_vector_t){0, 0, 0},
                           0.05f,
                           &sample);

    assert_near(1.0f, sample.accel_z_g);
    assert_near(0.0f, sample.roll_deg);
    assert_near(0.0f, sample.pitch_deg);
    assert(IMU_DEMO_MOTION_STILL == sample.motion);
}

static void test_gyro_scale_and_relative_yaw(void)
{
    imu_demo_filter_t filter = {0};
    imu_demo_sample_t sample = {0};

    imu_demo_filter_init(&filter);
    imu_demo_filter_update(&filter,
                           (imu_demo_raw_vector_t){0, 0, 8192},
                           (imu_demo_raw_vector_t){0, 0, 164},
                           0.1f,
                           &sample);

    assert_near(1.0f, sample.yaw_deg);
    assert_near(10.0f, sample.gyro_z_dps);
    assert(IMU_DEMO_MOTION_STILL == sample.motion);
}

static void test_motion_classification(void)
{
    imu_demo_filter_t filter = {0};
    imu_demo_sample_t sample = {0};

    imu_demo_filter_init(&filter);
    imu_demo_filter_update(&filter,
                           (imu_demo_raw_vector_t){0, 4096, 7094},
                           (imu_demo_raw_vector_t){0, 0, 0},
                           0.05f,
                           &sample);
    assert(IMU_DEMO_MOTION_TILTED == sample.motion);

    imu_demo_filter_update(&filter,
                           (imu_demo_raw_vector_t){0, 0, 8192},
                           (imu_demo_raw_vector_t){0, 0, 1640},
                           0.05f,
                           &sample);
    assert(IMU_DEMO_MOTION_ROTATING == sample.motion);
}

int main(void)
{
    test_level_accel_is_one_g();
    test_gyro_scale_and_relative_yaw();
    test_motion_classification();
    puts("imu_6axis_demo_logic_test: PASS");
    return 0;
}
