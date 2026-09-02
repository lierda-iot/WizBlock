#include "aec_test_logic.h"

#if defined(AEC_TEST_HOST_NO_CRT)
__declspec(dllimport) void __stdcall ExitProcess(unsigned int exit_code);
int _fltused = 0;
#endif

int printf(const char *format, ...);

#define TEST_TONE_DURATION_MS     5000U
#define TEST_RECOVERY_DURATION_MS 3000U
#define TEST_TONE_HZ              1000U
#define TEST_TONE_AMPLITUDE       12000
#define TEST_OUTPUT_VOLUME        100U
#define TEST_WAKENET_THRESHOLD    0.65f

static int s_failures = 0;

static void expect_true(const char *name, bool value)
{
    if (!value) {
        printf("FAIL: %s\n", name);
        s_failures++;
    }
}

static float test_absf(float value)
{
    return (value < 0.0f) ? -value : value;
}

static void test_passes_differ_only_by_aec_state(void)
{
    const aec_test_plan_config_t config = {
        .tone_duration_ms = TEST_TONE_DURATION_MS,
        .recovery_duration_ms = TEST_RECOVERY_DURATION_MS,
        .tone_hz = TEST_TONE_HZ,
        .tone_amplitude = TEST_TONE_AMPLITUDE,
        .output_volume = TEST_OUTPUT_VOLUME,
        .wakenet_enabled = true,
        .wakenet_threshold = TEST_WAKENET_THRESHOLD,
    };
    aec_test_pass_plan_t plans[AEC_TEST_PASS_COUNT] = {0};

    expect_true("plan is built", aec_test_build_pass_plan(&config, plans));
    expect_true("pass A enables AEC",
                AEC_TEST_PASS_AEC_ON == plans[0].id && plans[0].aec_enabled);
    expect_true("pass B disables AEC",
                AEC_TEST_PASS_AEC_OFF == plans[1].id && !plans[1].aec_enabled);
    expect_true("tone duration matches",
                plans[0].tone_duration_ms == plans[1].tone_duration_ms);
    expect_true("recovery duration matches",
                plans[0].recovery_duration_ms == plans[1].recovery_duration_ms);
    expect_true("tone frequency matches", plans[0].tone_hz == plans[1].tone_hz);
    expect_true("tone amplitude matches",
                plans[0].tone_amplitude == plans[1].tone_amplitude);
    expect_true("output volume matches",
                plans[0].output_volume == plans[1].output_volume);
    expect_true("both passes keep WakeNet enabled",
                plans[0].wakenet_enabled && plans[1].wakenet_enabled);
    expect_true("WakeNet threshold matches",
                plans[0].wakenet_threshold == plans[1].wakenet_threshold &&
                    TEST_WAKENET_THRESHOLD == plans[0].wakenet_threshold);
}

static void test_signal_metrics_cover_rms_peak_and_clipping(void)
{
    aec_test_signal_accumulator_t accumulator;
    aec_test_signal_metrics_t metrics = {0};

    aec_test_signal_reset(&accumulator);
    aec_test_signal_add(&accumulator, 3, 0, 0);
    aec_test_signal_add(&accumulator, -4, 0, 0);
    expect_true("clean signal metrics are available",
                aec_test_signal_summarize(&accumulator, &metrics));
    expect_true("RMS uses every sample", test_absf(metrics.rms - 3.5355f) < 0.001f);
    expect_true("peak uses absolute sample magnitude", 4 == metrics.peak);
    expect_true("clean signal has no clipping", 0.0f == metrics.clipping_ratio);
    expect_true("sample count is retained", 2U == metrics.sample_count);

    aec_test_signal_reset(&accumulator);
    aec_test_signal_add(&accumulator, 32767, 0, 0);
    aec_test_signal_add(&accumulator, -32768, 0, 0);
    expect_true("clipped signal metrics are available",
                aec_test_signal_summarize(&accumulator, &metrics));
    expect_true("negative full scale peak is represented", 32768 == metrics.peak);
    expect_true("both full-scale samples are clipped", 1.0f == metrics.clipping_ratio);
}

static void test_signal_metrics_measure_synchronous_tone_component(void)
{
    static const int16_t one_khz_at_sixteen_khz[] = {
        0, 383, 707, 924, 1000, 924, 707, 383,
        0, -383, -707, -924, -1000, -924, -707, -383,
    };
    aec_test_signal_accumulator_t accumulator;
    aec_test_signal_metrics_t metrics = {0};

    aec_test_signal_reset(&accumulator);
    for (uint32_t i = 0U;
         i < (uint32_t)(sizeof(one_khz_at_sixteen_khz) /
                        sizeof(one_khz_at_sixteen_khz[0]));
        i++) {
        aec_test_signal_add(&accumulator, one_khz_at_sixteen_khz[i],
                            one_khz_at_sixteen_khz[i], 0);
    }

    expect_true("tone metrics are available",
                aec_test_signal_summarize(&accumulator, &metrics));
    expect_true("aligned 1kHz projection equals signal RMS",
                test_absf(metrics.tone_rms - metrics.rms) < 0.01f);
}

static void test_tone_projection_is_independent_of_quarter_cycle_phase_shift(void)
{
    static const int16_t sine[] = {
        0, 383, 707, 924, 1000, 924, 707, 383,
        0, -383, -707, -924, -1000, -924, -707, -383,
    };
    static const int16_t cosine[] = {
        1000, 924, 707, 383, 0, -383, -707, -924,
        -1000, -924, -707, -383, 0, 383, 707, 924,
    };
    aec_test_signal_accumulator_t accumulator;
    aec_test_signal_metrics_t metrics = {0};

    aec_test_signal_reset(&accumulator);
    for (uint32_t i = 0U; i < 16U; i++) {
        aec_test_signal_add(&accumulator, cosine[i], sine[i], cosine[i]);
    }
    expect_true("quadrature tone metrics are available",
                aec_test_signal_summarize(&accumulator, &metrics));
    expect_true("quarter-cycle phase shift retains tone RMS",
                test_absf(metrics.tone_rms - metrics.rms) < 0.01f);
}

static void test_aec_tone_suppression_uses_off_to_on_ratio(void)
{
    float suppression_db = 0.0f;

    expect_true("suppression is available for positive levels",
                aec_test_compute_suppression_db(1000.0f, 100.0f,
                                                &suppression_db));
    expect_true("tenfold tone reduction is 20dB",
                test_absf(suppression_db - 20.0f) < 0.01f);
    expect_true("zero AEC ON level is rejected",
                !aec_test_compute_suppression_db(1000.0f, 0.0f,
                                                 &suppression_db));
}

static void test_afe_destroy_requires_all_tasks_to_join(void)
{
    aec_test_lifecycle_t lifecycle = {0};
    aec_test_lifecycle_begin(&lifecycle);
    expect_true("stop request enters stopping state",
                aec_test_lifecycle_request_stop(&lifecycle));
    expect_true("AFE destroy is blocked before joins",
                !aec_test_lifecycle_destroy_afe(&lifecycle));
    expect_true("feed exit alone is insufficient",
                !aec_test_lifecycle_mark_task_exited(&lifecycle,
                                                     AEC_TEST_TASK_FEED));
    expect_true("AFE destroy remains blocked with two tasks alive",
                !aec_test_lifecycle_destroy_afe(&lifecycle));
    expect_true("fetch exit still waits for tone",
                !aec_test_lifecycle_mark_task_exited(&lifecycle,
                                                     AEC_TEST_TASK_FETCH));
    expect_true("tone exit joins all tasks",
                aec_test_lifecycle_mark_task_exited(&lifecycle,
                                                    AEC_TEST_TASK_TONE));
    expect_true("AFE destroy is allowed after all joins",
                aec_test_lifecycle_destroy_afe(&lifecycle));
    expect_true("AFE is destroyed exactly once",
                !aec_test_lifecycle_destroy_afe(&lifecycle));
}

static void test_lifecycle_error_cannot_report_normal_destroy(void)
{
    aec_test_lifecycle_t lifecycle = {0};

    aec_test_lifecycle_begin(&lifecycle);
    aec_test_lifecycle_fail(&lifecycle);
    expect_true("lifecycle enters error state",
                AEC_TEST_LIFECYCLE_ERROR == lifecycle.state);
    expect_true("error state cannot report normal AFE destroy",
                !aec_test_lifecycle_destroy_afe(&lifecycle));
}

int main(void)
{
    test_passes_differ_only_by_aec_state();
    test_signal_metrics_cover_rms_peak_and_clipping();
    test_signal_metrics_measure_synchronous_tone_component();
    test_tone_projection_is_independent_of_quarter_cycle_phase_shift();
    test_aec_tone_suppression_uses_off_to_on_ratio();
    test_afe_destroy_requires_all_tasks_to_join();
    test_lifecycle_error_cannot_report_normal_destroy();
    if (0 == s_failures) {
        printf("aec_test_logic_test: PASS (0 failures)\n");
    } else {
        printf("aec_test_logic_test: FAIL (%d failures)\n", s_failures);
    }
    return s_failures;
}

#if defined(AEC_TEST_HOST_NO_CRT)
void mainCRTStartup(void)
{
    ExitProcess((unsigned int)main());
}
#endif
