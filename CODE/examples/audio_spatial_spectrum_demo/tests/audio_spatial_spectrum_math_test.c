#include "audio_spatial_spectrum_math.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#define TEST_EPSILON 0.0001f

static void assert_near(float actual, float expected)
{
    assert(fabsf(actual - expected) <= TEST_EPSILON);
}

static void assert_near_relaxed(float actual, float expected)
{
    assert(fabsf(actual - expected) <= 0.001f);
}

static void test_log_bands_are_valid_and_monotonic(void)
{
    audio_spectrum_band_t bands[AUDIO_SPECTRUM_BAND_COUNT] = {0};
    bool ok = audio_spectrum_build_log_bands(16000U, 512U, 80.0f, 8000.0f,
                                             bands, AUDIO_SPECTRUM_BAND_COUNT);

    assert(ok);
    assert(1U <= bands[0].first_bin);
    assert(255U >= bands[AUDIO_SPECTRUM_BAND_COUNT - 1U].last_bin);
    for (size_t index = 0; index < AUDIO_SPECTRUM_BAND_COUNT; index++) {
        assert(bands[index].first_bin <= bands[index].last_bin);
        if (0U < index) {
            assert((uint16_t)(bands[index - 1U].last_bin + 1U) ==
                   bands[index].first_bin);
        }
    }
}

static void test_invalid_band_configuration_is_rejected(void)
{
    audio_spectrum_band_t bands[AUDIO_SPECTRUM_BAND_COUNT] = {0};

    assert(!audio_spectrum_build_log_bands(0U, 512U, 80.0f, 8000.0f,
                                           bands, AUDIO_SPECTRUM_BAND_COUNT));
    assert(!audio_spectrum_build_log_bands(16000U, 32U, 80.0f, 8000.0f,
                                           bands, AUDIO_SPECTRUM_BAND_COUNT));
    assert(!audio_spectrum_build_log_bands(16000U, 512U, 8000.0f, 80.0f,
                                           bands, AUDIO_SPECTRUM_BAND_COUNT));
}

static void test_magnitude_and_db_mapping(void)
{
    assert_near(audio_spectrum_combine_magnitude(0.2f, 0.6f), 0.4f);
    assert_near(audio_spectrum_combine_magnitude(-1.0f, 0.6f), 0.3f);

    assert_near(audio_spectrum_level_from_db(-80.0f, -80.0f, -10.0f), 0.0f);
    assert_near(audio_spectrum_level_from_db(-45.0f, -80.0f, -10.0f), 0.5f);
    assert_near(audio_spectrum_level_from_db(-10.0f, -80.0f, -10.0f), 1.0f);
    assert_near(audio_spectrum_level_from_db(10.0f, -80.0f, -10.0f), 1.0f);
}

static void test_attack_release_and_peak_decay(void)
{
    audio_spectrum_envelope_t envelope = {0};
    float target[AUDIO_SPECTRUM_BAND_COUNT] = {0};
    target[0] = 1.0f;

    audio_spectrum_envelope_update(&envelope, target,
                                   AUDIO_SPECTRUM_BAND_COUNT,
                                   0.65f, 0.12f, 0.03f);
    assert_near(envelope.level[0], 0.65f);
    assert_near(envelope.peak[0], 0.65f);

    memset(target, 0, sizeof(target));
    audio_spectrum_envelope_update(&envelope, target,
                                   AUDIO_SPECTRUM_BAND_COUNT,
                                   0.65f, 0.12f, 0.03f);
    assert_near(envelope.level[0], 0.572f);
    assert_near(envelope.peak[0], 0.62f);
    assert(envelope.level[0] <= envelope.peak[0]);

    target[0] = 5.0f;
    audio_spectrum_envelope_update(&envelope, target,
                                   AUDIO_SPECTRUM_BAND_COUNT,
                                   0.65f, 0.12f, 0.03f);
    assert(0.0f <= envelope.level[0] && 1.0f >= envelope.level[0]);
    assert_near(envelope.peak[0], envelope.level[0]);
}

static void test_doa_display_mapping_is_clamped(void)
{
    assert_near(audio_spectrum_doa_relative(90.0f, 2.0f), 0.0f);
    assert_near(audio_spectrum_doa_relative(50.0f, 2.0f), 80.0f);
    assert_near(audio_spectrum_doa_relative(130.0f, 2.0f), -80.0f);
    assert_near(audio_spectrum_doa_relative(0.0f, 2.0f), 90.0f);
    assert_near(audio_spectrum_doa_relative(180.0f, 2.0f), -90.0f);
}

static void test_dbfs_and_band_average(void)
{
    const float levels[AUDIO_SPECTRUM_BAND_COUNT] = {
        0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f,
        0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f,
        0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f, 1.0f,
    };

    assert_near(audio_spectrum_average_band_levels(levels,
                                                   AUDIO_SPECTRUM_BAND_COUNT,
                                                   0U, 8U), 0.45f);
    assert_near(audio_spectrum_average_band_levels(levels,
                                                   AUDIO_SPECTRUM_BAND_COUNT,
                                                   8U, 8U), 0.55f);
    assert_near(audio_spectrum_average_band_levels(levels,
                                                   AUDIO_SPECTRUM_BAND_COUNT,
                                                   16U, 8U), 0.65f);
    assert_near(audio_spectrum_average_band_levels(levels,
                                                   AUDIO_SPECTRUM_BAND_COUNT,
                                                   23U, 2U), 1.0f);
    assert_near(audio_spectrum_average_band_levels(levels,
                                                   AUDIO_SPECTRUM_BAND_COUNT,
                                                   24U, 1U), 0.0f);

    assert_near(audio_spectrum_dbfs_from_rms_pair(0U, 0U, 32768U), -120.0f);
    assert_near_relaxed(audio_spectrum_dbfs_from_rms_pair(16384U, 16384U,
                                                           32768U), -6.0206f);
    assert_near(audio_spectrum_dbfs_from_rms_pair(32768U, 32768U, 32768U),
                0.0f);
    assert_near(audio_spectrum_dbfs_from_rms_pair(32768U, 32768U, 0U),
                -120.0f);
}

static void test_mode_step_wraps(void)
{
    assert(5U == audio_spectrum_mode_step(0U, -1, 6U));
    assert(1U == audio_spectrum_mode_step(0U, 1, 6U));
    assert(0U == audio_spectrum_mode_step(5U, 1, 6U));
    assert(4U == audio_spectrum_mode_step(5U, -1, 6U));
    assert(0U == audio_spectrum_mode_step(99U, 1, 6U));
    assert(0U == audio_spectrum_mode_step(3U, 1, 0U));
}

int main(void)
{
    test_log_bands_are_valid_and_monotonic();
    test_invalid_band_configuration_is_rejected();
    test_magnitude_and_db_mapping();
    test_attack_release_and_peak_decay();
    test_doa_display_mapping_is_clamped();
    test_dbfs_and_band_average();
    test_mode_step_wraps();
    puts("audio_spatial_spectrum_math_test: PASS");
    return 0;
}
