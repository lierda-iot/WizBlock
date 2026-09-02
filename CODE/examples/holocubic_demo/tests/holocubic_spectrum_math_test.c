#include "audio_spatial_spectrum_math.h"
#include "holocubic_spectrum_raster.h"
#include "holocubic_spectrum_visual_math.h"

#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
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
    for (size_t index = 0U; index < AUDIO_SPECTRUM_BAND_COUNT; index++) {
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

static void test_energy_rgb_is_bounded(void)
{
    const holocubic_spectrum_rgb_t low =
        holocubic_spectrum_energy_rgb(-1.0f, 0U);
    const holocubic_spectrum_rgb_t high =
        holocubic_spectrum_energy_rgb(2.0f, UINT8_MAX);
    const holocubic_spectrum_rgb_t invalid =
        holocubic_spectrum_energy_rgb(NAN, 0U);

    assert(30U == low.red);
    assert(50U == low.green);
    assert(90U == low.blue);
    assert(UINT8_MAX == high.red);
    assert(153U == high.green);
    assert(UINT8_MAX == high.blue);
    assert(30U == invalid.red);
    assert(50U == invalid.green);
    assert(90U == invalid.blue);
}

static uint32_t canvas_fingerprint(const uint16_t *canvas)
{
    uint32_t hash = 2166136261U;

    for (size_t index = 0U; index < HOLO_SPECTRUM_CANVAS_PIXELS; ++index) {
        hash ^= canvas[index];
        hash *= 16777619U;
    }
    return hash;
}

static size_t canvas_lit_pixels(const uint16_t *canvas)
{
    size_t count = 0U;

    for (size_t index = 0U; index < HOLO_SPECTRUM_CANVAS_PIXELS; ++index) {
        if (0U != canvas[index]) count++;
    }
    return count;
}

static void fill_visual_snapshot(holocubic_spectrum_snapshot_t *snapshot)
{
    *snapshot = (holocubic_spectrum_snapshot_t){
        .energy_db = 62.0f,
        .energy_dbfs = -18.0f,
        .relative_angle_deg = 34.0f,
        .mic1_level = 0.72f,
        .mic2_level = 0.48f,
        .revision = 7U,
        .doa_active = true,
        .available = true,
    };
    for (size_t index = 0U; index < AUDIO_SPECTRUM_BAND_COUNT; ++index) {
        const float rising = (float)(index + 1U) /
                             (float)AUDIO_SPECTRUM_BAND_COUNT;
        const float falling = 1.0f - rising * 0.72f;
        snapshot->combined_levels[index] =
            (0U == (index % 2U)) ? rising : falling;
        snapshot->combined_peaks[index] =
            fminf(1.0f, snapshot->combined_levels[index] + 0.12f);
        snapshot->mic1_levels[index] = rising;
        snapshot->mic1_peaks[index] = fminf(1.0f, rising + 0.08f);
        snapshot->mic2_levels[index] = falling;
        snapshot->mic2_peaks[index] = fminf(1.0f, falling + 0.08f);
    }
}

static void test_raster_modes_are_distinct_bounded_and_black_backed(void)
{
    enum { GUARD_PIXELS = 16 };
    uint16_t guarded[HOLO_SPECTRUM_CANVAS_PIXELS + GUARD_PIXELS * 2U];
    uint32_t fingerprints[HOLO_SPECTRUM_RASTER_MODE_COUNT] = {0};
    holocubic_spectrum_snapshot_t snapshot = {0};
    holocubic_spectrum_raster_state_t state = {0};

    fill_visual_snapshot(&snapshot);
    for (size_t mode = 0U; mode < HOLO_SPECTRUM_RASTER_MODE_COUNT; ++mode) {
        for (size_t index = 0U; index < sizeof(guarded) / sizeof(guarded[0]);
             ++index) {
            guarded[index] = 0xA55AU;
        }
        holocubic_spectrum_raster_reset(&state);
        holocubic_spectrum_raster_draw(
            &guarded[GUARD_PIXELS], &snapshot,
            (holocubic_spectrum_mode_t)mode, 2350U, &state);

        for (size_t index = 0U; index < GUARD_PIXELS; ++index) {
            assert(0xA55AU == guarded[index]);
            assert(0xA55AU == guarded[GUARD_PIXELS +
                                      HOLO_SPECTRUM_CANVAS_PIXELS + index]);
        }
        assert(0U == guarded[GUARD_PIXELS]);
        assert(800U < canvas_lit_pixels(&guarded[GUARD_PIXELS]));
        assert((HOLO_SPECTRUM_CANVAS_PIXELS * 2U / 3U) >
               canvas_lit_pixels(&guarded[GUARD_PIXELS]));
        fingerprints[mode] = canvas_fingerprint(&guarded[GUARD_PIXELS]);
        for (size_t previous = 0U; previous < mode; ++previous) {
            assert(fingerprints[previous] != fingerprints[mode]);
        }
    }
}

static void test_raster_state_responds_to_new_frames(void)
{
    uint16_t first[HOLO_SPECTRUM_CANVAS_PIXELS] = {0};
    uint16_t second[HOLO_SPECTRUM_CANVAS_PIXELS] = {0};
    holocubic_spectrum_snapshot_t snapshot = {0};
    holocubic_spectrum_raster_state_t state = {0};

    fill_visual_snapshot(&snapshot);
    holocubic_spectrum_raster_reset(&state);
    holocubic_spectrum_raster_draw(first, &snapshot,
                                   HOLO_SPECTRUM_WATERFALL, 1000U, &state);
    snapshot.revision++;
    for (size_t index = 0U; index < AUDIO_SPECTRUM_BAND_COUNT; ++index) {
        snapshot.combined_levels[index] = 0.05f;
    }
    holocubic_spectrum_raster_draw(second, &snapshot,
                                   HOLO_SPECTRUM_WATERFALL, 1100U, &state);
    assert(canvas_fingerprint(first) != canvas_fingerprint(second));

    holocubic_spectrum_raster_reset(&state);
    holocubic_spectrum_raster_draw(first, &snapshot,
                                   HOLO_SPECTRUM_METABALLS, 1000U, &state);
    holocubic_spectrum_raster_draw(second, &snapshot,
                                   HOLO_SPECTRUM_METABALLS, 1250U, &state);
    assert(canvas_fingerprint(first) != canvas_fingerprint(second));

    holocubic_spectrum_raster_reset(&state);
    snapshot.doa_active = true;
    snapshot.relative_angle_deg = 90.0f;
    holocubic_spectrum_raster_draw(first, &snapshot,
                                   HOLO_SPECTRUM_METABALLS, 1500U, &state);
    assert(2.5f + TEST_EPSILON >= fabsf(state.metaball_doa_offset));
    const float previous_offset = state.metaball_doa_offset;
    snapshot.relative_angle_deg = -90.0f;
    holocubic_spectrum_raster_draw(second, &snapshot,
                                   HOLO_SPECTRUM_METABALLS, 1600U, &state);
    assert(2.5f + TEST_EPSILON >=
           fabsf(state.metaball_doa_offset - previous_offset));

    holocubic_spectrum_raster_reset(&state);
    snapshot.doa_active = false;
    for (size_t band = 0U; band < AUDIO_SPECTRUM_BAND_COUNT; ++band) {
        snapshot.combined_levels[band] = 1.0f;
    }
    for (uint32_t frame = 0U; frame < 24U; ++frame) {
        holocubic_spectrum_raster_draw(
            second, &snapshot, HOLO_SPECTRUM_METABALLS,
            4000U + frame * 100U, &state);
    }
    for (size_t row = 216U; row < HOLO_SPECTRUM_CANVAS_HEIGHT; ++row) {
        for (size_t column = 0U; column < HOLO_SPECTRUM_CANVAS_WIDTH;
             ++column) {
            assert(0U == second[row * HOLO_SPECTRUM_CANVAS_WIDTH + column]);
        }
    }
}

static void write_ppm(const char *path, const uint16_t *canvas)
{
    FILE *file = fopen(path, "wb");
    assert(NULL != file);
    assert(0 < fprintf(file, "P6\n%u %u\n255\n",
                       (unsigned)HOLO_SPECTRUM_CANVAS_WIDTH,
                       (unsigned)HOLO_SPECTRUM_CANVAS_HEIGHT));
    for (size_t index = 0U; index < HOLO_SPECTRUM_CANVAS_PIXELS; ++index) {
        const uint16_t pixel = canvas[index];
        const uint8_t rgb[3] = {
            (uint8_t)((((pixel >> 11U) & 0x1FU) * 255U) / 31U),
            (uint8_t)((((pixel >> 5U) & 0x3FU) * 255U) / 63U),
            (uint8_t)(((pixel & 0x1FU) * 255U) / 31U),
        };
        assert(sizeof(rgb) == fwrite(rgb, 1U, sizeof(rgb), file));
    }
    assert(0 == fclose(file));
}

static void write_visual_previews(void)
{
    static const char *names[HOLO_SPECTRUM_RASTER_MODE_COUNT] = {
        "radar", "mirror", "waterfall", "metaballs", "level", "dual",
    };
    const char *directory = getenv("HOLO_SPECTRUM_PREVIEW_DIR");
    uint16_t canvas[HOLO_SPECTRUM_CANVAS_PIXELS] = {0};
    holocubic_spectrum_snapshot_t snapshot = {0};
    holocubic_spectrum_raster_state_t state = {0};
    char path[512] = {0};

    if (NULL == directory || '\0' == directory[0]) return;
    fill_visual_snapshot(&snapshot);
    for (size_t mode = 0U; mode < HOLO_SPECTRUM_RASTER_MODE_COUNT; ++mode) {
        holocubic_spectrum_raster_reset(&state);
        if (HOLO_SPECTRUM_WATERFALL == (holocubic_spectrum_mode_t)mode) {
            for (uint32_t frame = 0U;
                 frame < HOLO_SPECTRUM_WATERFALL_ROWS; ++frame) {
                snapshot.revision++;
                for (size_t band = 0U; band < AUDIO_SPECTRUM_BAND_COUNT;
                     ++band) {
                    snapshot.combined_levels[band] = 0.05f + 0.95f *
                        fabsf(sinf((float)(band + frame) * 0.24f));
                }
                holocubic_spectrum_raster_draw(
                    canvas, &snapshot, HOLO_SPECTRUM_WATERFALL,
                    frame * 100U, &state);
            }
        } else {
            holocubic_spectrum_raster_draw(
                canvas, &snapshot, (holocubic_spectrum_mode_t)mode,
                2350U, &state);
        }
        assert(0 < snprintf(path, sizeof(path), "%s/%s.ppm", directory,
                            names[mode]));
        write_ppm(path, canvas);
    }
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
    test_energy_rgb_is_bounded();
    test_raster_modes_are_distinct_bounded_and_black_backed();
    test_raster_state_responds_to_new_frames();
    write_visual_previews();
    puts("holocubic_spectrum_math_test: PASS");
    return 0;
}
