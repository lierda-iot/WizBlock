#include "companion_turn_control.h"

#include <math.h>
#include <stddef.h>

#define COMPANION_TURN_GYRO_LSB_PER_DPS 16.4f
#define COMPANION_TURN_STOP_LEAD_DEG 0.0f
#define COMPANION_TURN_MINIMUM_RATE_DPS 5.0f
#define COMPANION_TURN_RATE_DEAD_ZONE_DPS 2.0f
#define COMPANION_TURN_RATE_FILTER_ALPHA 0.25f
#define COMPANION_TURN_STALL_TIMEOUT_MS 400U
#define COMPANION_TURN_HARD_TIMEOUT_MS 8000U
#define COMPANION_IMU_AXIS_COUNT 3U

bool companion_imu_samples_stable(const companion_imu_sample_t *samples,
                                  size_t count, int16_t max_gyro_range_raw,
                                  float max_accel_range_raw)
{
    if (NULL == samples || 0U == count || 0 >= max_gyro_range_raw ||
        0.0f >= max_accel_range_raw) {
        return false;
    }
    int16_t gyro_min[3] = {samples[0].gyro_x_raw, samples[0].gyro_y_raw,
                           samples[0].gyro_z_raw};
    int16_t gyro_max[3] = {samples[0].gyro_x_raw, samples[0].gyro_y_raw,
                           samples[0].gyro_z_raw};
    float accel_min = samples[0].accel_magnitude_raw;
    float accel_max = accel_min;
    for (size_t index = 1U; index < count; ++index) {
        const int16_t gyro[3] = {samples[index].gyro_x_raw,
                                 samples[index].gyro_y_raw,
                                 samples[index].gyro_z_raw};
        for (size_t axis = 0U; axis < 3U; ++axis) {
            if (gyro[axis] < gyro_min[axis]) {
                gyro_min[axis] = gyro[axis];
            }
            if (gyro[axis] > gyro_max[axis]) {
                gyro_max[axis] = gyro[axis];
            }
        }
        if (samples[index].accel_magnitude_raw < accel_min) {
            accel_min = samples[index].accel_magnitude_raw;
        }
        if (samples[index].accel_magnitude_raw > accel_max) {
            accel_max = samples[index].accel_magnitude_raw;
        }
    }
    for (size_t axis = 0U; axis < 3U; ++axis) {
        if ((int32_t)gyro_max[axis] - gyro_min[axis] > max_gyro_range_raw) {
            return false;
        }
    }
    return accel_max - accel_min <= max_accel_range_raw;
}

esp_err_t companion_imu_estimate_gyro_bias(const int16_t *samples, size_t count,
                                           size_t trim_each_side,
                                           int16_t max_retained_range_raw,
                                           float *bias_raw)
{
    if (NULL == samples || NULL == bias_raw || 0U == count ||
        count > COMPANION_IMU_MAX_CALIBRATION_SAMPLES ||
        trim_each_side * 2U >= count || 0 >= max_retained_range_raw) {
        return ESP_ERR_INVALID_ARG;
    }
    int16_t sorted[COMPANION_IMU_MAX_CALIBRATION_SAMPLES] = {0};
    for (size_t index = 0U; index < count; ++index) {
        sorted[index] = samples[index];
    }
    for (size_t index = 1U; index < count; ++index) {
        const int16_t value = sorted[index];
        size_t position = index;
        while (0U < position && value < sorted[position - 1U]) {
            sorted[position] = sorted[position - 1U];
            position--;
        }
        sorted[position] = value;
    }
    const size_t first = trim_each_side;
    const size_t end = count - trim_each_side;
    if ((int32_t)sorted[end - 1U] - sorted[first] > max_retained_range_raw) {
        return ESP_ERR_INVALID_STATE;
    }
    int64_t sum = 0;
    for (size_t index = first; index < end; ++index) {
        sum += sorted[index];
    }
    *bias_raw = (float)sum / (float)(end - first);
    return ESP_OK;
}

esp_err_t companion_imu_estimate_calibration(
    const companion_imu_sample_t *samples, size_t count,
    size_t trim_each_side, int16_t max_retained_gyro_range_raw,
    companion_imu_calibration_t *calibration)
{
    if (NULL == samples || NULL == calibration || 0U == count ||
        count > COMPANION_IMU_MAX_CALIBRATION_SAMPLES ||
        trim_each_side * 2U >= count ||
        0 >= max_retained_gyro_range_raw) {
        return ESP_ERR_INVALID_ARG;
    }

    int16_t gyro_axis[COMPANION_IMU_MAX_CALIBRATION_SAMPLES] = {0};
    companion_imu_calibration_t estimate = {0};
    for (size_t axis = 0U; axis < COMPANION_IMU_AXIS_COUNT; ++axis) {
        for (size_t index = 0U; index < count; ++index) {
            const int16_t gyro[COMPANION_IMU_AXIS_COUNT] = {
                samples[index].gyro_x_raw,
                samples[index].gyro_y_raw,
                samples[index].gyro_z_raw,
            };
            gyro_axis[index] = gyro[axis];
        }
        const esp_err_t result = companion_imu_estimate_gyro_bias(
            gyro_axis, count, trim_each_side,
            max_retained_gyro_range_raw, &estimate.gyro_bias_raw[axis]);
        if (ESP_OK != result) {
            return result;
        }
    }

    float acceleration_sum[COMPANION_IMU_AXIS_COUNT] = {0.0f};
    for (size_t index = 0U; index < count; ++index) {
        acceleration_sum[0] += samples[index].accel_x_raw;
        acceleration_sum[1] += samples[index].accel_y_raw;
        acceleration_sum[2] += samples[index].accel_z_raw;
    }
    const float acceleration_norm = sqrtf(
        acceleration_sum[0] * acceleration_sum[0] +
        acceleration_sum[1] * acceleration_sum[1] +
        acceleration_sum[2] * acceleration_sum[2]);
    if (!isfinite(acceleration_norm) || 0.0f >= acceleration_norm) {
        return ESP_ERR_INVALID_STATE;
    }
    for (size_t axis = 0U; axis < COMPANION_IMU_AXIS_COUNT; ++axis) {
        estimate.gravity_unit[axis] =
            acceleration_sum[axis] / acceleration_norm;
    }
    *calibration = estimate;
    return ESP_OK;
}

void companion_turn_control_config_default(companion_turn_control_config_t *config)
{
    if (NULL != config) {
        *config = (companion_turn_control_config_t){
            .gyro_lsb_per_dps = COMPANION_TURN_GYRO_LSB_PER_DPS,
            .stop_lead_deg = COMPANION_TURN_STOP_LEAD_DEG,
            .minimum_rate_dps = COMPANION_TURN_MINIMUM_RATE_DPS,
            .rate_dead_zone_dps = COMPANION_TURN_RATE_DEAD_ZONE_DPS,
            .rate_filter_alpha = COMPANION_TURN_RATE_FILTER_ALPHA,
            .stall_timeout_ms = COMPANION_TURN_STALL_TIMEOUT_MS,
            .hard_timeout_ms = COMPANION_TURN_HARD_TIMEOUT_MS,
        };
    }
}

esp_err_t companion_turn_control_start(companion_turn_control_t *control,
                                       const companion_turn_control_config_t *config,
                                       float target_deg,
                                       const companion_imu_calibration_t *calibration)
{
    if (NULL == control || NULL == config || NULL == calibration ||
        !isfinite(target_deg) || 0.0f >= target_deg ||
        0.0f >= config->gyro_lsb_per_dps || 0.0f > config->stop_lead_deg ||
        0.0f > config->rate_dead_zone_dps ||
        0.0f >= config->rate_filter_alpha ||
        1.0f < config->rate_filter_alpha || 0U == config->hard_timeout_ms) {
        return ESP_ERR_INVALID_ARG;
    }

    float gravity_norm_squared = 0.0f;
    for (size_t axis = 0U; axis < COMPANION_IMU_AXIS_COUNT; ++axis) {
        if (!isfinite(calibration->gyro_bias_raw[axis]) ||
            !isfinite(calibration->gravity_unit[axis])) {
            return ESP_ERR_INVALID_ARG;
        }
        gravity_norm_squared += calibration->gravity_unit[axis] *
                                calibration->gravity_unit[axis];
    }
    if (0.0f >= gravity_norm_squared) {
        return ESP_ERR_INVALID_ARG;
    }
    const float gravity_norm = sqrtf(gravity_norm_squared);
    *control = (companion_turn_control_t){
        .calibration = *calibration,
        .target_deg = target_deg,
        .active = true,
    };
    for (size_t axis = 0U; axis < COMPANION_IMU_AXIS_COUNT; ++axis) {
        control->calibration.gravity_unit[axis] /= gravity_norm;
    }
    return ESP_OK;
}

esp_err_t companion_turn_control_update_sample_us(
    companion_turn_control_t *control,
    const companion_turn_control_config_t *config,
    const companion_imu_sample_t *sample, uint32_t dt_us, bool *complete)
{
    if (NULL == control || NULL == config || NULL == complete ||
        NULL == sample || !control->active || 0U == dt_us ||
        100000U < dt_us) {
        return ESP_ERR_INVALID_ARG;
    }
    *complete = false;
    const float gyro_raw[COMPANION_IMU_AXIS_COUNT] = {
        sample->gyro_x_raw,
        sample->gyro_y_raw,
        sample->gyro_z_raw,
    };
    float projected_raw = 0.0f;
    for (size_t axis = 0U; axis < COMPANION_IMU_AXIS_COUNT; ++axis) {
        projected_raw +=
            (gyro_raw[axis] - control->calibration.gyro_bias_raw[axis]) *
            control->calibration.gravity_unit[axis];
    }
    control->projected_rate_dps = projected_raw / config->gyro_lsb_per_dps;
    float rate_dps = fabsf(control->projected_rate_dps);
    if (rate_dps < config->rate_dead_zone_dps) {
        rate_dps = 0.0f;
    }
    control->filtered_rate_dps += config->rate_filter_alpha *
                                  (rate_dps - control->filtered_rate_dps);
    control->elapsed_us += dt_us;
    control->elapsed_ms = (uint32_t)(control->elapsed_us / 1000ULL);
    const float current_integrated_rate =
        (control->filtered_rate_dps >= config->minimum_rate_dps) ?
        control->filtered_rate_dps : 0.0f;
    const float previous_integrated_rate =
        (control->has_previous_rate &&
         control->previous_filtered_rate_dps >= config->minimum_rate_dps) ?
        control->previous_filtered_rate_dps : 0.0f;
    if (0.0f < current_integrated_rate) {
        control->turned_deg +=
            (previous_integrated_rate + current_integrated_rate) * 0.5f *
            ((float)dt_us / 1000000.0f);
        control->still_us = 0ULL;
        control->still_ms = 0U;
    } else {
        control->still_us += dt_us;
        control->still_ms = (uint32_t)(control->still_us / 1000ULL);
    }
    control->previous_filtered_rate_dps = control->filtered_rate_dps;
    control->has_previous_rate = true;
    const float stop_at_deg = fmaxf(
        0.0f, control->target_deg - config->stop_lead_deg);
    if (control->turned_deg >= stop_at_deg) {
        control->active = false;
        *complete = true;
        return ESP_OK;
    }
    if (control->still_us >=
            (uint64_t)config->stall_timeout_ms * 1000ULL) {
        control->failure_reason = COMPANION_TURN_FAILURE_STALL;
        control->active = false;
        return ESP_ERR_TIMEOUT;
    }
    if (control->elapsed_us >=
            (uint64_t)config->hard_timeout_ms * 1000ULL) {
        control->failure_reason = COMPANION_TURN_FAILURE_HARD_TIMEOUT;
        control->active = false;
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

float companion_turn_control_remaining_deg(
    const companion_turn_control_t *control)
{
    if (NULL == control || !isfinite(control->target_deg) ||
        !isfinite(control->turned_deg)) {
        return 0.0f;
    }
    return fmaxf(0.0f, control->target_deg - control->turned_deg);
}
