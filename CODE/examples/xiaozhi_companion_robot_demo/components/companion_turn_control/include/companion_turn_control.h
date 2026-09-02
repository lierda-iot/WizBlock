#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define COMPANION_IMU_MAX_CALIBRATION_SAMPLES 64U

typedef struct {
    int16_t gyro_x_raw;
    int16_t gyro_y_raw;
    int16_t gyro_z_raw;
    int16_t accel_x_raw;
    int16_t accel_y_raw;
    int16_t accel_z_raw;
    float accel_magnitude_raw;
} companion_imu_sample_t;

typedef struct {
    float gyro_bias_raw[3];
    float gravity_unit[3];
} companion_imu_calibration_t;

typedef struct {
    float gyro_lsb_per_dps;
    float stop_lead_deg;
    float minimum_rate_dps;
    float rate_dead_zone_dps;
    float rate_filter_alpha;
    uint32_t stall_timeout_ms;
    uint32_t hard_timeout_ms;
} companion_turn_control_config_t;

typedef enum {
    COMPANION_TURN_FAILURE_NONE = 0,
    COMPANION_TURN_FAILURE_STALL,
    COMPANION_TURN_FAILURE_HARD_TIMEOUT,
} companion_turn_failure_reason_t;

typedef struct {
    companion_imu_calibration_t calibration;
    float target_deg;
    float turned_deg;
    float projected_rate_dps;
    float filtered_rate_dps;
    float previous_filtered_rate_dps;
    uint64_t elapsed_us;
    uint64_t still_us;
    uint32_t elapsed_ms;
    uint32_t still_ms;
    companion_turn_failure_reason_t failure_reason;
    bool has_previous_rate;
    bool active;
} companion_turn_control_t;

bool companion_imu_samples_stable(const companion_imu_sample_t *samples,
                                  size_t count, int16_t max_gyro_range_raw,
                                  float max_accel_range_raw);
esp_err_t companion_imu_estimate_gyro_bias(const int16_t *samples, size_t count,
                                           size_t trim_each_side,
                                           int16_t max_retained_range_raw,
                                           float *bias_raw);
esp_err_t companion_imu_estimate_calibration(
    const companion_imu_sample_t *samples, size_t count,
    size_t trim_each_side, int16_t max_retained_gyro_range_raw,
    companion_imu_calibration_t *calibration);
void companion_turn_control_config_default(companion_turn_control_config_t *config);
esp_err_t companion_turn_control_start(companion_turn_control_t *control,
                                       const companion_turn_control_config_t *config,
                                       float target_deg,
                                       const companion_imu_calibration_t *calibration);
esp_err_t companion_turn_control_update_sample_us(
    companion_turn_control_t *control,
    const companion_turn_control_config_t *config,
    const companion_imu_sample_t *sample, uint32_t dt_us, bool *complete);
float companion_turn_control_remaining_deg(
    const companion_turn_control_t *control);
