#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
    uint64_t timestamp_us;
} companion_merit_sample_t;

typedef struct {
    uint32_t accel_delta_threshold_raw;
    uint32_t baseline_stability_threshold_raw;
    uint32_t return_threshold_raw;
    uint32_t confirm_window_us;
    uint32_t cooldown_us;
    uint8_t baseline_window_samples;
    uint8_t noise_multiplier;
} companion_merit_tap_config_t;

typedef struct {
    bool hit;
    uint32_t sample_seq;
    int32_t accel_peak_raw;
    int32_t gyro_peak_raw;
    uint32_t repeat_count;
} companion_merit_result_t;

typedef struct {
    companion_merit_tap_config_t config;
    uint32_t accel_history[8];
    uint32_t baseline_accel_raw;
    uint32_t previous_accel_raw;
    uint64_t last_timestamp_us;
    uint64_t candidate_deadline_us;
    uint64_t last_hit_us;
    uint32_t sample_seq;
    uint32_t repeat_count;
    uint32_t candidate_excursion_peak_raw;
    uint32_t candidate_gyro_peak_raw;
    uint32_t last_baseline_range_raw;
    uint32_t last_excursion_raw;
    uint32_t last_dynamic_threshold_raw;
    uint8_t history_count;
    uint8_t history_index;
    bool initialized;
    bool candidate_active;
    bool has_last_hit;
} companion_merit_tap_t;

void companion_merit_tap_config_default(
    companion_merit_tap_config_t *config);
esp_err_t companion_merit_tap_init(
    companion_merit_tap_t *detector,
    const companion_merit_tap_config_t *config);
void companion_merit_tap_reset(companion_merit_tap_t *detector);
esp_err_t companion_merit_tap_push(
    companion_merit_tap_t *detector,
    const companion_merit_sample_t *sample,
    companion_merit_result_t *result);
