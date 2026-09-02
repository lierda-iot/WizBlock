#include "aec_test_logic.h"

#include <math.h>
#include <stddef.h>

#define AEC_TEST_ALL_TASKS (AEC_TEST_TASK_FEED | AEC_TEST_TASK_FETCH | AEC_TEST_TASK_TONE)

bool aec_test_build_pass_plan(const aec_test_plan_config_t *config,
                              aec_test_pass_plan_t plans[AEC_TEST_PASS_COUNT])
{
    if (NULL == config || NULL == plans) {
        return false;
    }

    plans[0] = (aec_test_pass_plan_t){
        .id = AEC_TEST_PASS_AEC_ON,
        .aec_enabled = true,
        .tone_duration_ms = config->tone_duration_ms,
        .recovery_duration_ms = config->recovery_duration_ms,
        .tone_hz = config->tone_hz,
        .tone_amplitude = config->tone_amplitude,
        .output_volume = config->output_volume,
        .wakenet_enabled = config->wakenet_enabled,
        .wakenet_threshold = config->wakenet_threshold,
    };
    plans[1] = plans[0];
    plans[1].id = AEC_TEST_PASS_AEC_OFF;
    plans[1].aec_enabled = false;
    return true;
}

void aec_test_signal_reset(aec_test_signal_accumulator_t *accumulator)
{
    if (NULL == accumulator) {
        return;
    }
    *accumulator = (aec_test_signal_accumulator_t){0};
}

void aec_test_signal_add(aec_test_signal_accumulator_t *accumulator,
                         int16_t sample, int16_t tone_in_phase,
                         int16_t tone_quadrature)
{
    if (NULL == accumulator) {
        return;
    }

    const int32_t sample_32 = sample;
    const int32_t in_phase_32 = tone_in_phase;
    const int32_t quadrature_32 = tone_quadrature;
    const int32_t absolute_sample = (sample_32 < 0) ? -sample_32 : sample_32;
    accumulator->sum_squares += (uint64_t)((int64_t)sample_32 * sample_32);
    accumulator->tone_in_phase_dot_product += (int64_t)sample_32 * in_phase_32;
    accumulator->tone_quadrature_dot_product += (int64_t)sample_32 * quadrature_32;
    accumulator->tone_in_phase_sum_squares +=
        (uint64_t)((int64_t)in_phase_32 * in_phase_32);
    accumulator->tone_quadrature_sum_squares +=
        (uint64_t)((int64_t)quadrature_32 * quadrature_32);
    if (absolute_sample > accumulator->peak) {
        accumulator->peak = absolute_sample;
    }
    if (INT16_MAX == sample || INT16_MIN == sample) {
        accumulator->clipped_count++;
    }
    accumulator->sample_count++;
}

bool aec_test_signal_summarize(const aec_test_signal_accumulator_t *accumulator,
                               aec_test_signal_metrics_t *metrics)
{
    if (NULL == accumulator || NULL == metrics || 0U == accumulator->sample_count) {
        return false;
    }

    metrics->rms = sqrtf((float)accumulator->sum_squares /
                         (float)accumulator->sample_count);
    metrics->tone_rms = 0.0f;
    float projected_energy = 0.0f;
    if (0U != accumulator->tone_in_phase_sum_squares) {
        const float tone_dot = (float)accumulator->tone_in_phase_dot_product;
        projected_energy += (tone_dot * tone_dot) /
                            (float)accumulator->tone_in_phase_sum_squares;
    }
    if (0U != accumulator->tone_quadrature_sum_squares) {
        const float tone_dot = (float)accumulator->tone_quadrature_dot_product;
        projected_energy += (tone_dot * tone_dot) /
                            (float)accumulator->tone_quadrature_sum_squares;
    }
    if (projected_energy > 0.0f) {
        metrics->tone_rms = sqrtf(projected_energy /
                                  (float)accumulator->sample_count);
    }
    metrics->clipping_ratio = (float)accumulator->clipped_count /
                              (float)accumulator->sample_count;
    metrics->peak = accumulator->peak;
    metrics->sample_count = accumulator->sample_count;
    return true;
}

bool aec_test_compute_suppression_db(float aec_off_tone_rms,
                                     float aec_on_tone_rms,
                                     float *suppression_db)
{
    if (NULL == suppression_db || aec_off_tone_rms <= 0.0f ||
        aec_on_tone_rms <= 0.0f) {
        return false;
    }
    *suppression_db = 20.0f * log10f(aec_off_tone_rms / aec_on_tone_rms);
    return true;
}

void aec_test_lifecycle_begin(aec_test_lifecycle_t *lifecycle)
{
    if (NULL == lifecycle) {
        return;
    }
    lifecycle->state = AEC_TEST_LIFECYCLE_CAPTURE;
    lifecycle->exited_tasks = 0U;
}

bool aec_test_lifecycle_request_stop(aec_test_lifecycle_t *lifecycle)
{
    if (NULL == lifecycle || AEC_TEST_LIFECYCLE_CAPTURE != lifecycle->state) {
        return false;
    }
    lifecycle->state = AEC_TEST_LIFECYCLE_STOP_REQUESTED;
    return true;
}

bool aec_test_lifecycle_mark_task_exited(aec_test_lifecycle_t *lifecycle,
                                         uint8_t task_mask)
{
    if (NULL == lifecycle ||
        AEC_TEST_LIFECYCLE_STOP_REQUESTED != lifecycle->state ||
        0U == task_mask || 0U != (task_mask & (uint8_t)~AEC_TEST_ALL_TASKS) ||
        0U != (lifecycle->exited_tasks & task_mask)) {
        return false;
    }

    lifecycle->exited_tasks |= task_mask;
    if (AEC_TEST_ALL_TASKS == lifecycle->exited_tasks) {
        lifecycle->state = AEC_TEST_LIFECYCLE_TASKS_JOINED;
        return true;
    }
    return false;
}

bool aec_test_lifecycle_destroy_afe(aec_test_lifecycle_t *lifecycle)
{
    if (NULL == lifecycle ||
        AEC_TEST_LIFECYCLE_TASKS_JOINED != lifecycle->state) {
        return false;
    }
    lifecycle->state = AEC_TEST_LIFECYCLE_AFE_DESTROYED;
    return true;
}

void aec_test_lifecycle_fail(aec_test_lifecycle_t *lifecycle)
{
    if (NULL != lifecycle &&
        AEC_TEST_LIFECYCLE_AFE_DESTROYED != lifecycle->state) {
        lifecycle->state = AEC_TEST_LIFECYCLE_ERROR;
    }
}
