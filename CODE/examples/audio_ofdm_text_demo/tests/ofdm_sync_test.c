#include "ofdm_sync.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_PREFIX_SAMPLES 137U
#define TEST_SUFFIX_SAMPLES 384U
#define TEST_CAPTURE_SAMPLES \
    (TEST_PREFIX_SAMPLES + OFDM_FRAME_SAMPLE_COUNT + TEST_SUFFIX_SAMPLES)
#define TEST_CHANNEL_DELAY_FIRST 7U
#define TEST_CHANNEL_DELAY_SECOND 19U
#define TEST_PATH_GAIN_DIRECT 0.60F
#define TEST_PATH_GAIN_FIRST 0.24F
#define TEST_PATH_GAIN_SECOND -0.10F
#define TEST_NOISE_AMPLITUDE 0.0004F
#define TEST_NOISE_DIVISOR 128.0F
#define TEST_LCG_SEED UINT32_C(0x534E4354)
#define TEST_LCG_MULTIPLIER UINT32_C(1664525)
#define TEST_LCG_INCREMENT UINT32_C(1013904223)
#define TEST_LOUD_NOISE_AMPLITUDE 0.02F
#define TEST_RESIDUE_PREFIX_SAMPLES 128U
#define TEST_CHIRP_GROUP_DELAY_SAMPLES 24U
#define TEST_CHIRP_EARLY_SAMPLES 96U
#define TEST_MARGINAL_CHIRP_NOISE_AMPLITUDE 0.50F

static void fill_payload(uint8_t *payload, size_t length)
{
    assert(NULL != payload);
    for (size_t index = 0U; index < length; ++index) {
        payload[index] = (uint8_t)(index * 17U + 3U);
    }
}

static void fill_header(uint8_t *header, size_t length)
{
    assert(NULL != header);
    for (size_t index = 0U; index < length; ++index) {
        header[index] = (uint8_t)(index * 13U + 5U);
    }
}

static void apply_test_channel(const float *input,
                               float *output,
                               size_t length)
{
    assert(NULL != input);
    assert(NULL != output);

    uint32_t noise_state = TEST_LCG_SEED;
    for (size_t index = 0U; index < length; ++index) {
        float sample = input[index] * TEST_PATH_GAIN_DIRECT;
        if (TEST_CHANNEL_DELAY_FIRST <= index) {
            sample += input[index - TEST_CHANNEL_DELAY_FIRST] *
                      TEST_PATH_GAIN_FIRST;
        }
        if (TEST_CHANNEL_DELAY_SECOND <= index) {
            sample += input[index - TEST_CHANNEL_DELAY_SECOND] *
                      TEST_PATH_GAIN_SECOND;
        }
        noise_state = noise_state * TEST_LCG_MULTIPLIER +
                      TEST_LCG_INCREMENT;
        const int32_t noise_code =
            (int32_t)((noise_state >> 24U) & UINT32_C(0xFF)) - 128;
        output[index] = sample +
                        (float)noise_code / TEST_NOISE_DIVISOR *
                            TEST_NOISE_AMPLITUDE;
    }
}

static void build_frame(float frame[OFDM_FRAME_SAMPLE_COUNT])
{
    uint8_t header[OFDM_FRAME_HEADER_BYTES] = {0};
    uint8_t payload[OFDM_CODED_PAYLOAD_BYTES] = {0};

    fill_payload(payload, sizeof(payload));
    fill_header(header, sizeof(header));
    assert(OFDM_PHY_OK == ofdm_phy_modulate_frame(header, payload, frame));
}

static void test_exact_offset_and_scale(void)
{
    static float frame[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    static float capture[TEST_CAPTURE_SAMPLES] = {0.0F};
    ofdm_sync_match_t match = {0};

    build_frame(frame);
    memcpy(&capture[TEST_PREFIX_SAMPLES], frame, sizeof(frame));
    for (size_t index = TEST_PREFIX_SAMPLES;
         index < TEST_PREFIX_SAMPLES + OFDM_FRAME_SAMPLE_COUNT; ++index) {
        capture[index] *= 0.37F;
    }
    assert(OFDM_SYNC_OK == ofdm_sync_find_frame(
                               capture, sizeof(capture) / sizeof(capture[0]),
                               &match));
    assert(TEST_PREFIX_SAMPLES == match.frame_offset);
    assert(TEST_PREFIX_SAMPLES + OFDM_FRAME_CHIRP_OFFSET ==
           match.chirp_offset);
    assert(0.99F < match.chirp_score);
    assert(1.0e-6F > fabsf(match.best_chirp_score - match.chirp_score));
    assert(OFDM_FFT_SIZE / 2U == match.sc_period_lag);
    assert(0.99F < match.sc_period_score);
    assert(OFDM_FFT_SIZE == match.lts_short_period_lag);
    assert(0.99F < match.lts_short_period_score);
    assert(OFDM_SYMBOL_SAMPLES == match.lts_long_period_lag);
    assert(0.99F < match.lts_long_period_score);
}

static void test_coarse_step_residues(void)
{
    static float frame[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    static float capture[TEST_CAPTURE_SAMPLES] = {0.0F};

    build_frame(frame);
    for (size_t residue = 0U; residue < OFDM_SYNC_COARSE_STEP; ++residue) {
        const size_t frame_offset = TEST_RESIDUE_PREFIX_SAMPLES + residue;
        ofdm_sync_match_t match = {0};
        memset(capture, 0, sizeof(capture));
        memcpy(&capture[frame_offset], frame, sizeof(frame));
        assert(OFDM_SYNC_OK == ofdm_sync_find_frame(
                                   capture,
                                   sizeof(capture) / sizeof(capture[0]),
                                   &match));
        assert(frame_offset == match.frame_offset);
        assert(0.99F < match.chirp_score);
    }
}

static void test_chirp_group_delay_timing_refinement(void)
{
    static float frame[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    static float delayed_frame[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    static float capture[TEST_CAPTURE_SAMPLES] = {0.0F};
    static uint8_t decoded_header[OFDM_FRAME_HEADER_BYTES] = {0};
    static uint8_t decoded_payload[OFDM_CODED_PAYLOAD_BYTES] = {0};
    ofdm_sync_match_t match = {0};
    ofdm_phy_frame_metrics_t metrics = {0};

    build_frame(frame);
    memcpy(delayed_frame, frame, sizeof(delayed_frame));
    memset(&delayed_frame[OFDM_FRAME_CHIRP_OFFSET], 0,
           OFDM_CHIRP_SAMPLES * sizeof(delayed_frame[0]));
    memcpy(&delayed_frame[OFDM_FRAME_CHIRP_OFFSET +
                          TEST_CHIRP_GROUP_DELAY_SAMPLES],
           &frame[OFDM_FRAME_CHIRP_OFFSET],
           OFDM_CHIRP_SAMPLES * sizeof(frame[0]));
    memcpy(&capture[TEST_PREFIX_SAMPLES], delayed_frame,
           sizeof(delayed_frame));

    assert(OFDM_SYNC_OK == ofdm_sync_find_frame(
                               capture, sizeof(capture) / sizeof(capture[0]),
                               &match));
    assert(TEST_PREFIX_SAMPLES == match.frame_offset);
    assert(-(int32_t)TEST_CHIRP_GROUP_DELAY_SAMPLES ==
           match.timing_correction_samples);
    assert(TEST_PREFIX_SAMPLES + OFDM_FRAME_CHIRP_OFFSET +
               TEST_CHIRP_GROUP_DELAY_SAMPLES ==
           match.chirp_offset);
    assert(OFDM_PHY_OK == ofdm_phy_demodulate_frame(
                              &capture[match.frame_offset], decoded_header,
                              decoded_payload, &metrics));
}

static void test_chirp_early_timing_refinement(void)
{
    static float frame[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    static float capture[TEST_CAPTURE_SAMPLES] = {0.0F};
    static uint8_t decoded_header[OFDM_FRAME_HEADER_BYTES] = {0};
    static uint8_t decoded_payload[OFDM_CODED_PAYLOAD_BYTES] = {0};
    ofdm_sync_match_t match = {0};
    ofdm_phy_frame_metrics_t metrics = {0};

    build_frame(frame);
    memcpy(&capture[TEST_PREFIX_SAMPLES], frame, sizeof(frame));
    memset(&capture[TEST_PREFIX_SAMPLES + OFDM_FRAME_CHIRP_OFFSET], 0,
           OFDM_CHIRP_SAMPLES * sizeof(capture[0]));
    memcpy(&capture[TEST_PREFIX_SAMPLES + OFDM_FRAME_CHIRP_OFFSET -
                    TEST_CHIRP_EARLY_SAMPLES],
           &frame[OFDM_FRAME_CHIRP_OFFSET],
           OFDM_CHIRP_SAMPLES * sizeof(frame[0]));

    assert(OFDM_SYNC_OK == ofdm_sync_find_frame(
                               capture, sizeof(capture) / sizeof(capture[0]),
                               &match));
    assert(TEST_PREFIX_SAMPLES == match.frame_offset);
    assert((int32_t)TEST_CHIRP_EARLY_SAMPLES ==
           match.timing_correction_samples);
    assert(TEST_PREFIX_SAMPLES + OFDM_FRAME_CHIRP_OFFSET -
               TEST_CHIRP_EARLY_SAMPLES ==
           match.chirp_offset);
    assert(OFDM_PHY_OK == ofdm_phy_demodulate_frame(
                              &capture[match.frame_offset], decoded_header,
                              decoded_payload, &metrics));
}

static void test_composite_training_accepts_marginal_chirp(void)
{
    static float frame[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    static float capture[TEST_CAPTURE_SAMPLES] = {0.0F};
    ofdm_sync_match_t match = {0};
    uint32_t noise_state = TEST_LCG_SEED;

    build_frame(frame);
    for (size_t index = 0U; index < OFDM_CHIRP_SAMPLES; ++index) {
        noise_state = noise_state * TEST_LCG_MULTIPLIER +
                      TEST_LCG_INCREMENT;
        const int32_t noise_code =
            (int32_t)((noise_state >> 24U) & UINT32_C(0xFF)) - 128;
        frame[OFDM_FRAME_CHIRP_OFFSET + index] +=
            (float)noise_code / TEST_NOISE_DIVISOR *
            TEST_MARGINAL_CHIRP_NOISE_AMPLITUDE;
    }
    memcpy(&capture[TEST_PREFIX_SAMPLES], frame, sizeof(frame));

    assert(OFDM_SYNC_OK == ofdm_sync_find_frame(
                               capture, sizeof(capture) / sizeof(capture[0]),
                               &match));
    assert(OFDM_SYNC_COMPOSITE_MIN_SCORE <= match.chirp_score);
    assert(OFDM_SYNC_MIN_SCORE > match.chirp_score);
    assert(match.used_training_match);
    assert(TEST_PREFIX_SAMPLES == match.frame_offset);
}

static void test_noise_and_rejection(void)
{
    static float frame[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    static float capture[TEST_CAPTURE_SAMPLES] = {0.0F};
    ofdm_sync_match_t match = {0};

    build_frame(frame);
    memcpy(&capture[TEST_PREFIX_SAMPLES], frame, sizeof(frame));
    for (size_t index = TEST_PREFIX_SAMPLES;
         index < TEST_PREFIX_SAMPLES + OFDM_FRAME_SAMPLE_COUNT; ++index) {
        const uint32_t value = (uint32_t)(index * 1103515245U + 12345U);
        const float noise = (float)((value >> 24U) & 0xFFU) / 255.0F - 0.5F;
        capture[index] = capture[index] * 0.5F + noise * 0.004F;
    }
    assert(OFDM_SYNC_OK == ofdm_sync_find_frame(
                               capture, sizeof(capture) / sizeof(capture[0]),
                               &match));
    assert(TEST_PREFIX_SAMPLES == match.frame_offset);
    assert(0.75F < match.chirp_score);

    memset(capture, 0, sizeof(capture));
    assert(OFDM_SYNC_NOT_FOUND == ofdm_sync_find_frame(
                                      capture,
                                      sizeof(capture) / sizeof(capture[0]),
                                      &match));
    assert(OFDM_SYNC_NOT_FOUND == ofdm_sync_find_frame(
                                      capture, OFDM_FRAME_SAMPLE_COUNT - 1U,
                                      &match));
}

static void test_channel_offset_and_demodulation(void)
{
    static float frame[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    static float channel_output[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    static float capture[TEST_CAPTURE_SAMPLES] = {0.0F};
    static uint8_t header[OFDM_FRAME_HEADER_BYTES] = {0};
    static uint8_t payload[OFDM_CODED_PAYLOAD_BYTES] = {0};
    static uint8_t decoded_header[OFDM_FRAME_HEADER_BYTES] = {0};
    static uint8_t decoded_payload[OFDM_CODED_PAYLOAD_BYTES] = {0};
    ofdm_sync_match_t match = {0};
    ofdm_phy_frame_metrics_t metrics = {0};

    fill_header(header, sizeof(header));
    fill_payload(payload, sizeof(payload));
    assert(OFDM_PHY_OK == ofdm_phy_modulate_frame(header, payload, frame));
    apply_test_channel(frame, channel_output,
                       sizeof(channel_output) / sizeof(channel_output[0]));
    memcpy(&capture[TEST_PREFIX_SAMPLES], channel_output,
           sizeof(channel_output));

    assert(OFDM_SYNC_OK == ofdm_sync_find_frame(
                               capture, sizeof(capture) / sizeof(capture[0]),
                               &match));
    assert(TEST_PREFIX_SAMPLES == match.frame_offset);
    assert(0.70F < match.chirp_score);
    assert(OFDM_PHY_OK == ofdm_phy_demodulate_frame(
                              &capture[match.frame_offset], decoded_header,
                              decoded_payload, &metrics));
    assert(0 == memcmp(header, decoded_header, sizeof(header)));
    assert(0 == memcmp(payload, decoded_payload, sizeof(payload)));
}

static void test_loud_noise_rejection(void)
{
    static float capture[TEST_CAPTURE_SAMPLES] = {0.0F};
    ofdm_sync_match_t match = {.best_chirp_score = -1.0F};
    uint32_t noise_state = TEST_LCG_SEED;

    for (size_t index = 0U; index < sizeof(capture) / sizeof(capture[0]);
         ++index) {
        noise_state = noise_state * TEST_LCG_MULTIPLIER +
                      TEST_LCG_INCREMENT;
        const int32_t noise_code =
            (int32_t)((noise_state >> 24U) & UINT32_C(0xFF)) - 128;
        capture[index] = (float)noise_code / TEST_NOISE_DIVISOR *
                         TEST_LOUD_NOISE_AMPLITUDE;
    }
    assert(OFDM_SYNC_NOT_FOUND == ofdm_sync_find_frame(
                                      capture,
                                      sizeof(capture) / sizeof(capture[0]),
                                      &match));
    assert(0.0F <= match.best_chirp_score);
    assert(OFDM_SYNC_MIN_SCORE > match.best_chirp_score);
}

int main(void)
{
    static float frame[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};

    assert(OFDM_SYNC_NOT_INITIALIZED == ofdm_sync_find_frame(
                                            frame,
                                            sizeof(frame) / sizeof(frame[0]),
                                            &(ofdm_sync_match_t){0}));
    assert(OFDM_PHY_OK == ofdm_phy_init());
    assert(OFDM_SYNC_OK == ofdm_sync_init());
    test_exact_offset_and_scale();
    test_coarse_step_residues();
    test_chirp_group_delay_timing_refinement();
    test_chirp_early_timing_refinement();
    test_composite_training_accepts_marginal_chirp();
    test_noise_and_rejection();
    test_channel_offset_and_demodulation();
    test_loud_noise_rejection();
    ofdm_sync_deinit();
    ofdm_phy_deinit();
    puts("ofdm_sync_test: PASS");
    return 0;
}
