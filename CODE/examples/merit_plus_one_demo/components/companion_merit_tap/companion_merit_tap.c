#include "companion_merit_tap.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define COMPANION_MERIT_DEFAULT_ACCEL_DELTA_RAW 400U
#define COMPANION_MERIT_DEFAULT_BASELINE_STABILITY_RAW 500U
#define COMPANION_MERIT_DEFAULT_RETURN_RAW 350U
#define COMPANION_MERIT_DEFAULT_CONFIRM_WINDOW_US 150000U
#define COMPANION_MERIT_DEFAULT_COOLDOWN_US 120000U
#define COMPANION_MERIT_DEFAULT_BASELINE_WINDOW_SAMPLES 5U
#define COMPANION_MERIT_DEFAULT_NOISE_MULTIPLIER 2U
#define COMPANION_MERIT_MIN_BASELINE_WINDOW_SAMPLES 3U
#define COMPANION_MERIT_MAX_BASELINE_WINDOW_SAMPLES 8U
#define COMPANION_MERIT_MAX_NOISE_MULTIPLIER 8U

static bool config_valid(const companion_merit_tap_config_t *config)
{
    return NULL != config && 0U != config->accel_delta_threshold_raw &&
           0U != config->baseline_stability_threshold_raw &&
           0U != config->return_threshold_raw &&
           config->return_threshold_raw < config->accel_delta_threshold_raw &&
           0U != config->confirm_window_us && 0U != config->cooldown_us &&
           COMPANION_MERIT_MIN_BASELINE_WINDOW_SAMPLES <=
               config->baseline_window_samples &&
           COMPANION_MERIT_MAX_BASELINE_WINDOW_SAMPLES >=
               config->baseline_window_samples &&
           0U != config->noise_multiplier &&
           COMPANION_MERIT_MAX_NOISE_MULTIPLIER >=
               config->noise_multiplier;
}

static uint32_t vector_magnitude_raw(int16_t x, int16_t y, int16_t z)
{
    const int64_t x_value = x;
    const int64_t y_value = y;
    const int64_t z_value = z;
    const uint64_t square_sum = (uint64_t)(x_value * x_value) +
                                (uint64_t)(y_value * y_value) +
                                (uint64_t)(z_value * z_value);
    const double magnitude = sqrt((double)square_sum);
    return (UINT32_MAX < magnitude) ? UINT32_MAX : (uint32_t)magnitude;
}

static uint32_t absolute_delta(uint32_t left, uint32_t right)
{
    return (left >= right) ? (left - right) : (right - left);
}

static void reset_candidate(companion_merit_tap_t *detector)
{
    detector->candidate_active = false;
    detector->candidate_deadline_us = 0ULL;
    detector->candidate_excursion_peak_raw = 0U;
    detector->candidate_gyro_peak_raw = 0U;
}

static void reset_history(companion_merit_tap_t *detector)
{
    memset(detector->accel_history, 0, sizeof(detector->accel_history));
    detector->history_count = 0U;
    detector->history_index = 0U;
}

static void add_history(companion_merit_tap_t *detector, uint32_t accel_raw)
{
    detector->accel_history[detector->history_index] = accel_raw;
    detector->history_index++;
    if (detector->history_index >= detector->config.baseline_window_samples) {
        detector->history_index = 0U;
    }
    if (detector->history_count < detector->config.baseline_window_samples) {
        detector->history_count++;
    }
}

static void history_stats(const companion_merit_tap_t *detector,
                          uint32_t *median_raw, uint32_t *range_raw)
{
    uint32_t sorted[COMPANION_MERIT_MAX_BASELINE_WINDOW_SAMPLES] = {0};
    const uint8_t count = detector->config.baseline_window_samples;
    for (uint8_t index = 0U; index < count; ++index) {
        sorted[index] = detector->accel_history[index];
    }
    for (uint8_t index = 1U; index < count; ++index) {
        const uint32_t value = sorted[index];
        uint8_t target = index;
        while (0U < target && value < sorted[target - 1U]) {
            sorted[target] = sorted[target - 1U];
            target--;
        }
        sorted[target] = value;
    }
    *median_raw = sorted[count / 2U];
    *range_raw = sorted[count - 1U] - sorted[0U];
}

static uint32_t dynamic_excursion_threshold(
    const companion_merit_tap_t *detector, uint32_t baseline_range_raw)
{
    const uint64_t noise_threshold =
        (uint64_t)baseline_range_raw * detector->config.noise_multiplier;
    if (noise_threshold > UINT32_MAX) {
        return UINT32_MAX;
    }
    return (uint32_t)noise_threshold >
            detector->config.accel_delta_threshold_raw ?
        (uint32_t)noise_threshold :
        detector->config.accel_delta_threshold_raw;
}

void companion_merit_tap_config_default(
    companion_merit_tap_config_t *config)
{
    if (NULL == config) {
        return;
    }
    *config = (companion_merit_tap_config_t){
        .accel_delta_threshold_raw = COMPANION_MERIT_DEFAULT_ACCEL_DELTA_RAW,
        .baseline_stability_threshold_raw =
            COMPANION_MERIT_DEFAULT_BASELINE_STABILITY_RAW,
        .return_threshold_raw = COMPANION_MERIT_DEFAULT_RETURN_RAW,
        .confirm_window_us = COMPANION_MERIT_DEFAULT_CONFIRM_WINDOW_US,
        .cooldown_us = COMPANION_MERIT_DEFAULT_COOLDOWN_US,
        .baseline_window_samples =
            COMPANION_MERIT_DEFAULT_BASELINE_WINDOW_SAMPLES,
        .noise_multiplier = COMPANION_MERIT_DEFAULT_NOISE_MULTIPLIER,
    };
}

esp_err_t companion_merit_tap_init(
    companion_merit_tap_t *detector,
    const companion_merit_tap_config_t *config)
{
    if (NULL == detector || !config_valid(config)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(detector, 0, sizeof(*detector));
    detector->config = *config;
    return ESP_OK;
}

void companion_merit_tap_reset(companion_merit_tap_t *detector)
{
    if (NULL == detector) {
        return;
    }
    const companion_merit_tap_config_t config = detector->config;
    memset(detector, 0, sizeof(*detector));
    detector->config = config;
}

esp_err_t companion_merit_tap_push(
    companion_merit_tap_t *detector,
    const companion_merit_sample_t *sample,
    companion_merit_result_t *result)
{
    if (NULL == detector || NULL == sample || NULL == result ||
        !config_valid(&detector->config)) {
        return ESP_ERR_INVALID_ARG;
    }
    *result = (companion_merit_result_t){0};
    if (detector->initialized && sample->timestamp_us <
        detector->last_timestamp_us) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t accel_raw = vector_magnitude_raw(
        sample->accel_x, sample->accel_y, sample->accel_z);
    const uint32_t gyro_raw = vector_magnitude_raw(
        sample->gyro_x, sample->gyro_y, sample->gyro_z);
    detector->sample_seq++;
    if (0U == detector->sample_seq) {
        detector->sample_seq = 1U;
    }
    result->sample_seq = detector->sample_seq;

    if (!detector->initialized) {
        detector->initialized = true;
        detector->baseline_accel_raw = accel_raw;
        detector->previous_accel_raw = accel_raw;
        detector->last_timestamp_us = sample->timestamp_us;
        add_history(detector, accel_raw);
        return ESP_OK;
    }
    detector->last_timestamp_us = sample->timestamp_us;

    if (detector->candidate_active && sample->timestamp_us >
        detector->candidate_deadline_us) {
        reset_candidate(detector);
        reset_history(detector);
        add_history(detector, accel_raw);
        detector->baseline_accel_raw = accel_raw;
        detector->previous_accel_raw = accel_raw;
        return ESP_OK;
    }

    if (detector->candidate_active) {
        const uint32_t excursion_raw = absolute_delta(
            accel_raw, detector->baseline_accel_raw);
        detector->last_excursion_raw = excursion_raw;
        if (excursion_raw > detector->candidate_excursion_peak_raw) {
            detector->candidate_excursion_peak_raw = excursion_raw;
        }
        if (gyro_raw > detector->candidate_gyro_peak_raw) {
            detector->candidate_gyro_peak_raw = gyro_raw;
        }
        if (excursion_raw <= detector->config.return_threshold_raw) {
            result->hit = true;
            result->accel_peak_raw =
                (int32_t)detector->candidate_excursion_peak_raw;
            result->gyro_peak_raw =
                (int32_t)detector->candidate_gyro_peak_raw;
            if (detector->has_last_hit &&
                sample->timestamp_us - detector->last_hit_us <=
                    detector->config.cooldown_us) {
                detector->repeat_count++;
            } else {
                detector->repeat_count = 0U;
            }
            result->repeat_count = detector->repeat_count;
            detector->has_last_hit = true;
            detector->last_hit_us = sample->timestamp_us;
            reset_candidate(detector);
            reset_history(detector);
            add_history(detector, accel_raw);
        }
        detector->previous_accel_raw = accel_raw;
        return ESP_OK;
    }

    if (detector->history_count >=
        detector->config.baseline_window_samples) {
        uint32_t baseline_raw = 0U;
        uint32_t baseline_range_raw = 0U;
        history_stats(detector, &baseline_raw, &baseline_range_raw);
        detector->baseline_accel_raw = baseline_raw;
        const uint32_t excursion_raw = absolute_delta(accel_raw,
                                                      baseline_raw);
        const uint32_t threshold_raw = dynamic_excursion_threshold(
            detector, baseline_range_raw);
        detector->last_baseline_range_raw = baseline_range_raw;
        detector->last_excursion_raw = excursion_raw;
        detector->last_dynamic_threshold_raw = threshold_raw;
        if (baseline_range_raw <=
                detector->config.baseline_stability_threshold_raw &&
            excursion_raw >= threshold_raw) {
            detector->candidate_active = true;
            detector->candidate_deadline_us = sample->timestamp_us +
                detector->config.confirm_window_us;
            detector->candidate_excursion_peak_raw = excursion_raw;
            detector->candidate_gyro_peak_raw = gyro_raw;
            detector->previous_accel_raw = accel_raw;
            return ESP_OK;
        }
    }

    add_history(detector, accel_raw);
    detector->previous_accel_raw = accel_raw;
    return ESP_OK;
}
