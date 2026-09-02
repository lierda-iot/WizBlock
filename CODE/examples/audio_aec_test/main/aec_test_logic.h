#pragma once

#include <stdbool.h>
#include <stdint.h>

#define AEC_TEST_PASS_COUNT 2U
#define AEC_TEST_TASK_FEED  (1U << 0)
#define AEC_TEST_TASK_FETCH (1U << 1)
#define AEC_TEST_TASK_TONE  (1U << 2)

typedef enum {
    AEC_TEST_PASS_AEC_ON = 0,
    AEC_TEST_PASS_AEC_OFF,
} aec_test_pass_id_t;

typedef struct {
    uint32_t tone_duration_ms;
    uint32_t recovery_duration_ms;
    uint32_t tone_hz;
    int16_t tone_amplitude;
    uint8_t output_volume;
    bool wakenet_enabled;
    float wakenet_threshold;
} aec_test_plan_config_t;

typedef struct {
    aec_test_pass_id_t id;
    bool aec_enabled;
    uint32_t tone_duration_ms;
    uint32_t recovery_duration_ms;
    uint32_t tone_hz;
    int16_t tone_amplitude;
    uint8_t output_volume;
    bool wakenet_enabled;
    float wakenet_threshold;
} aec_test_pass_plan_t;

typedef struct {
    uint64_t sum_squares;
    int64_t tone_in_phase_dot_product;
    int64_t tone_quadrature_dot_product;
    uint64_t tone_in_phase_sum_squares;
    uint64_t tone_quadrature_sum_squares;
    int32_t peak;
    uint32_t sample_count;
    uint32_t clipped_count;
} aec_test_signal_accumulator_t;

typedef struct {
    float rms;
    float tone_rms;
    float clipping_ratio;
    int32_t peak;
    uint32_t sample_count;
} aec_test_signal_metrics_t;

typedef enum {
    AEC_TEST_LIFECYCLE_CAPTURE = 0,
    AEC_TEST_LIFECYCLE_STOP_REQUESTED,
    AEC_TEST_LIFECYCLE_TASKS_JOINED,
    AEC_TEST_LIFECYCLE_AFE_DESTROYED,
    AEC_TEST_LIFECYCLE_ERROR,
} aec_test_lifecycle_state_t;

typedef struct {
    aec_test_lifecycle_state_t state;
    uint8_t exited_tasks;
} aec_test_lifecycle_t;

bool aec_test_build_pass_plan(const aec_test_plan_config_t *config,
                              aec_test_pass_plan_t plans[AEC_TEST_PASS_COUNT]);
void aec_test_signal_reset(aec_test_signal_accumulator_t *accumulator);
void aec_test_signal_add(aec_test_signal_accumulator_t *accumulator,
                         int16_t sample, int16_t tone_in_phase,
                         int16_t tone_quadrature);
bool aec_test_signal_summarize(const aec_test_signal_accumulator_t *accumulator,
                               aec_test_signal_metrics_t *metrics);
bool aec_test_compute_suppression_db(float aec_off_tone_rms,
                                     float aec_on_tone_rms,
                                     float *suppression_db);
void aec_test_lifecycle_begin(aec_test_lifecycle_t *lifecycle);
bool aec_test_lifecycle_request_stop(aec_test_lifecycle_t *lifecycle);
bool aec_test_lifecycle_mark_task_exited(aec_test_lifecycle_t *lifecycle,
                                         uint8_t task_mask);
bool aec_test_lifecycle_destroy_afe(aec_test_lifecycle_t *lifecycle);
void aec_test_lifecycle_fail(aec_test_lifecycle_t *lifecycle);
