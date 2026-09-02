#include "ofdm_phy.h"

#include "ofdm_frame.h"

#include <kiss_fft.h>

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_MULTIPATH_DELAY_FIRST 7U
#define TEST_MULTIPATH_DELAY_SECOND 19U
#define TEST_PATH_GAIN_DIRECT 0.60F
#define TEST_PATH_GAIN_FIRST 0.24F
#define TEST_PATH_GAIN_SECOND -0.10F
#define TEST_NOISE_AMPLITUDE 0.0004F
#define TEST_NOISE_DIVISOR 128.0F
#define TEST_LCG_SEED UINT32_C(0x4F46444D)
#define TEST_LCG_MULTIPLIER UINT32_C(1664525)
#define TEST_LCG_INCREMENT UINT32_C(1013904223)
#define TEST_CHANNEL_SESSION_ID UINT16_C(0x6789)
#define TEST_REJECT_NOISE_AMPLITUDE 0.02F
#define TEST_MODERATE_NOISE_AMPLITUDE 0.009F
#define TEST_FADED_FIRST_BIN 27U
#define TEST_FADED_LAST_BIN 32U

static void fill_payload(uint8_t *payload, size_t length)
{
    assert(NULL != payload);
    for (size_t index = 0U; index < length; ++index) {
        payload[index] = (uint8_t)(index * 29U + 7U);
    }
}

static float calculate_rms(const float *samples, size_t sample_count)
{
    float energy = 0.0F;
    for (size_t index = 0U; index < sample_count; ++index) {
        energy += samples[index] * samples[index];
    }
    return sqrtf(energy / (float)sample_count);
}

static void apply_test_channel(const float *input,
                               float *output,
                               size_t length,
                               float noise_amplitude)
{
    assert(NULL != input);
    assert(NULL != output);

    uint32_t noise_state = TEST_LCG_SEED;
    for (size_t index = 0U; index < length; ++index) {
        float sample = input[index] * TEST_PATH_GAIN_DIRECT;
        if (TEST_MULTIPATH_DELAY_FIRST <= index) {
            sample += input[index - TEST_MULTIPATH_DELAY_FIRST] *
                      TEST_PATH_GAIN_FIRST;
        }
        if (TEST_MULTIPATH_DELAY_SECOND <= index) {
            sample += input[index - TEST_MULTIPATH_DELAY_SECOND] *
                      TEST_PATH_GAIN_SECOND;
        }

        noise_state = noise_state * TEST_LCG_MULTIPLIER +
                      TEST_LCG_INCREMENT;
        const int32_t noise_code =
            (int32_t)((noise_state >> 24U) & UINT32_C(0xFF)) - 128;
        output[index] = sample +
                        (float)noise_code / TEST_NOISE_DIVISOR *
                            noise_amplitude;
    }
}

static void corrupt_header_carrier_band(float *samples)
{
    assert(NULL != samples);

    kiss_fft_cfg forward_fft = kiss_fft_alloc(
        (int)OFDM_FFT_SIZE, 0, NULL, NULL);
    kiss_fft_cfg inverse_fft = kiss_fft_alloc(
        (int)OFDM_FFT_SIZE, 1, NULL, NULL);
    assert(NULL != forward_fft);
    assert(NULL != inverse_fft);

    kiss_fft_cpx time_samples[OFDM_FFT_SIZE] = {{0.0F, 0.0F}};
    kiss_fft_cpx frequency_bins[OFDM_FFT_SIZE] = {{0.0F, 0.0F}};
    kiss_fft_cpx reconstructed[OFDM_FFT_SIZE] = {{0.0F, 0.0F}};
    for (size_t symbol_index = 0U;
         symbol_index < OFDM_HEADER_SYMBOL_COUNT; ++symbol_index) {
        const size_t symbol_offset = OFDM_FRAME_HEADER_OFFSET +
            symbol_index * OFDM_SYMBOL_SAMPLES;
        for (size_t index = 0U; index < OFDM_FFT_SIZE; ++index) {
            time_samples[index].r =
                samples[symbol_offset + OFDM_CP_SAMPLES + index];
            time_samples[index].i = 0.0F;
        }
        kiss_fft(forward_fft, time_samples, frequency_bins);

        for (uint16_t bin = TEST_FADED_FIRST_BIN;
             bin <= TEST_FADED_LAST_BIN; ++bin) {
            if (ofdm_phy_is_pilot_bin(bin)) {
                continue;
            }
            frequency_bins[bin].r = -frequency_bins[bin].r;
            frequency_bins[bin].i = -frequency_bins[bin].i;
            const uint16_t conjugate_bin = (uint16_t)(OFDM_FFT_SIZE - bin);
            frequency_bins[conjugate_bin].r =
                -frequency_bins[conjugate_bin].r;
            frequency_bins[conjugate_bin].i =
                -frequency_bins[conjugate_bin].i;
        }

        kiss_fft(inverse_fft, frequency_bins, reconstructed);
        for (size_t index = 0U; index < OFDM_FFT_SIZE; ++index) {
            samples[symbol_offset + OFDM_CP_SAMPLES + index] =
                reconstructed[index].r / (float)OFDM_FFT_SIZE;
        }
        for (size_t index = 0U; index < OFDM_CP_SAMPLES; ++index) {
            samples[symbol_offset + index] =
                samples[symbol_offset + OFDM_FFT_SIZE + index];
        }
    }

    kiss_fft_free(forward_fft);
    kiss_fft_free(inverse_fft);
}

static void test_carrier_layout(void)
{
    size_t data_index = 0U;

    assert(32U == OFDM_FIRST_CARRIER_BIN);
    assert(59U == OFDM_LAST_CARRIER_BIN);
    assert(24U == OFDM_DATA_CARRIER_COUNT);
    for (uint16_t bin = OFDM_FIRST_CARRIER_BIN;
         bin <= OFDM_LAST_CARRIER_BIN; ++bin) {
        if (ofdm_phy_is_pilot_bin(bin)) {
            continue;
        }
        assert(data_index < OFDM_DATA_CARRIER_COUNT);
        assert(bin == ofdm_phy_get_data_bin(data_index));
        ++data_index;
    }
    assert(OFDM_DATA_CARRIER_COUNT == data_index);
    assert(0U == ofdm_phy_get_data_bin(OFDM_DATA_CARRIER_COUNT));
    assert(ofdm_phy_is_pilot_bin(34U));
    assert(ofdm_phy_is_pilot_bin(42U));
    assert(ofdm_phy_is_pilot_bin(50U));
    assert(ofdm_phy_is_pilot_bin(58U));
    assert(!ofdm_phy_is_pilot_bin(35U));
}

static void test_payload_round_trip(void)
{
    static uint8_t original[OFDM_CODED_PAYLOAD_BYTES] = {0};
    static uint8_t decoded[OFDM_CODED_PAYLOAD_BYTES] = {0};
    static float samples[OFDM_PAYLOAD_SAMPLE_COUNT] = {0.0F};
    float evm_db = 0.0F;
    float peak = 0.0F;

    fill_payload(original, sizeof(original));
    assert(OFDM_PHY_NOT_INITIALIZED ==
           ofdm_phy_modulate_payload(original, samples));
    assert(OFDM_PHY_OK == ofdm_phy_init());
    assert(OFDM_PHY_OK == ofdm_phy_modulate_payload(original, samples));

    for (size_t sample_index = 0U;
         sample_index < OFDM_PAYLOAD_SAMPLE_COUNT; ++sample_index) {
        assert(isfinite(samples[sample_index]));
        const float magnitude = fabsf(samples[sample_index]);
        peak = magnitude > peak ? magnitude : peak;
    }
    assert(0.01F < peak);
    assert(0.5F > peak);

    for (size_t symbol = 0U; symbol < OFDM_PAYLOAD_SYMBOL_COUNT; ++symbol) {
        const size_t base = symbol * OFDM_SYMBOL_SAMPLES;
        for (size_t cp_index = 0U; cp_index < OFDM_CP_SAMPLES; ++cp_index) {
            assert(1.0e-7F > fabsf(
                       samples[base + cp_index] -
                       samples[base + OFDM_FFT_SIZE + cp_index]));
        }
    }

    assert(OFDM_PHY_OK ==
           ofdm_phy_demodulate_payload(samples, decoded, &evm_db));
    assert(0 == memcmp(original, decoded, sizeof(original)));
    assert(-100.0F > evm_db);

    assert(OFDM_PHY_INVALID_ARGUMENT ==
           ofdm_phy_modulate_payload(NULL, samples));
    assert(OFDM_PHY_INVALID_ARGUMENT ==
           ofdm_phy_demodulate_payload(samples, NULL, &evm_db));
    ofdm_phy_deinit();
}

static void test_fec_phy_round_trip(void)
{
    static uint8_t original[OFDM_FEC_DATA_BYTES] = {0};
    static uint8_t encoded[OFDM_CODED_PAYLOAD_BYTES] = {0};
    static uint8_t received[OFDM_CODED_PAYLOAD_BYTES] = {0};
    static uint8_t decoded[OFDM_FEC_DATA_BYTES] = {0};
    static float samples[OFDM_PAYLOAD_SAMPLE_COUNT] = {0.0F};
    uint16_t corrected_symbols = UINT16_MAX;
    float evm_db = 0.0F;

    fill_payload(original, sizeof(original));
    assert(OFDM_FEC_OK == ofdm_fec_init());
    assert(OFDM_PHY_OK == ofdm_phy_init());
    assert(OFDM_FEC_OK == ofdm_fec_encode(
                              original, encoded, 0x4567U, 5U));
    assert(OFDM_PHY_OK == ofdm_phy_modulate_payload(encoded, samples));
    assert(OFDM_PHY_OK ==
           ofdm_phy_demodulate_payload(samples, received, &evm_db));
    assert(0 == memcmp(encoded, received, sizeof(encoded)));
    assert(OFDM_FEC_OK == ofdm_fec_decode(
                              received, decoded, 0x4567U, 5U,
                              &corrected_symbols));
    assert(0U == corrected_symbols);
    assert(0 == memcmp(original, decoded, sizeof(original)));
    assert(-100.0F > evm_db);
    ofdm_phy_deinit();
    ofdm_fec_deinit();
}

static void test_complete_frame_round_trip(void)
{
    static uint8_t message[OFDM_FEC_DATA_BYTES] = {0};
    static uint8_t header_wire[OFDM_FRAME_HEADER_BYTES] = {0};
    static uint8_t decoded_header[OFDM_FRAME_HEADER_BYTES] = {0};
    static uint8_t encoded[OFDM_CODED_PAYLOAD_BYTES] = {0};
    static uint8_t decoded_encoded[OFDM_CODED_PAYLOAD_BYTES] = {0};
    static uint8_t decoded_payload[OFDM_FEC_DATA_BYTES] = {0};
    static float samples[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    ofdm_frame_t frame = {0};
    ofdm_frame_header_t parsed_header = {0};
    ofdm_phy_frame_metrics_t metrics = {0};
    uint16_t corrected_symbols = UINT16_MAX;
    float peak = 0.0F;

    for (size_t index = 0U; index < sizeof(message); ++index) {
        message[index] = (uint8_t)('A' + (index % 26U));
    }
    assert(OFDM_FRAME_OK == ofdm_frame_build(
                                message, sizeof(message), 0x5678U, 0U,
                                &frame));
    assert(OFDM_FRAME_OK ==
           ofdm_frame_header_serialize(&frame.header, header_wire));
    assert(OFDM_FEC_OK == ofdm_fec_init());
    assert(OFDM_PHY_OK == ofdm_phy_init());
    assert(OFDM_FEC_OK == ofdm_fec_encode(
                              frame.payload, encoded,
                              frame.header.session_id,
                              frame.header.frame_index));
    assert(OFDM_PHY_OK == ofdm_phy_modulate_frame(
                              header_wire, encoded, samples));

    for (size_t sample_index = 0U;
         sample_index < OFDM_FRAME_SAMPLE_COUNT; ++sample_index) {
        assert(isfinite(samples[sample_index]));
        const float magnitude = fabsf(samples[sample_index]);
        peak = magnitude > peak ? magnitude : peak;
    }
    assert(0.05F < peak);
    assert(1.0F > peak);
    assert(0.03F < calculate_rms(
                       &samples[OFDM_FRAME_SC_OFFSET],
                       OFDM_SYMBOL_SAMPLES));
    assert(0.04F < calculate_rms(
                       &samples[OFDM_FRAME_LTS_OFFSET],
                       OFDM_LTS_SYMBOL_COUNT * OFDM_SYMBOL_SAMPLES));
    assert(0.03F < calculate_rms(
                       &samples[OFDM_FRAME_HEADER_OFFSET],
                       OFDM_HEADER_SYMBOL_COUNT * OFDM_SYMBOL_SAMPLES));
    assert(0.08F < calculate_rms(
                       &samples[OFDM_FRAME_PAYLOAD_OFFSET],
                       OFDM_PAYLOAD_SAMPLE_COUNT));
    for (size_t index = 0U; index < OFDM_GUARD_SAMPLES; ++index) {
        assert(0.0F == samples[index]);
        assert(0.0F == samples[OFDM_FRAME_SAMPLE_COUNT - 1U - index]);
    }

    const size_t sc_body = OFDM_FRAME_SC_OFFSET + OFDM_CP_SAMPLES;
    for (size_t index = 0U; index < OFDM_FFT_SIZE / 2U; ++index) {
        assert(1.0e-6F > fabsf(samples[sc_body + index] -
                                     samples[sc_body + OFDM_FFT_SIZE / 2U +
                                             index]));
    }
    for (size_t symbol_index = 1U;
         symbol_index < OFDM_LTS_SYMBOL_COUNT; ++symbol_index) {
        for (size_t index = 0U; index < OFDM_SYMBOL_SAMPLES; ++index) {
            assert(1.0e-6F > fabsf(
                       samples[OFDM_FRAME_LTS_OFFSET + index] -
                       samples[OFDM_FRAME_LTS_OFFSET +
                               symbol_index * OFDM_SYMBOL_SAMPLES +
                               index]));
        }
    }

    assert(OFDM_PHY_OK == ofdm_phy_demodulate_frame(
                              samples, decoded_header, decoded_encoded,
                              &metrics));
    assert(0 == memcmp(header_wire, decoded_header, sizeof(header_wire)));
    assert(0 == memcmp(encoded, decoded_encoded, sizeof(encoded)));
    assert(0.99F < metrics.sc_score);
    assert(0.99F < metrics.lts_score);
    assert(-80.0F > metrics.header_evm_db);
    assert(-80.0F > metrics.payload_evm_db);
    assert(OFDM_FRAME_OK ==
           ofdm_frame_header_parse(decoded_header, &parsed_header));
    assert(0 == memcmp(&frame.header, &parsed_header,
                       sizeof(parsed_header)));
    assert(OFDM_FEC_OK == ofdm_fec_decode(
                              decoded_encoded, decoded_payload,
                              parsed_header.session_id,
                              parsed_header.frame_index,
                              &corrected_symbols));
    assert(0U == corrected_symbols);
    assert(0 == memcmp(frame.payload, decoded_payload,
                       sizeof(decoded_payload)));
    ofdm_phy_deinit();
    ofdm_fec_deinit();
}

static void test_frame_through_short_multipath_channel(void)
{
    static uint8_t message[OFDM_FEC_DATA_BYTES] = {0};
    static uint8_t header_wire[OFDM_FRAME_HEADER_BYTES] = {0};
    static uint8_t decoded_header[OFDM_FRAME_HEADER_BYTES] = {0};
    static uint8_t encoded[OFDM_CODED_PAYLOAD_BYTES] = {0};
    static uint8_t received[OFDM_CODED_PAYLOAD_BYTES] = {0};
    static uint8_t decoded_payload[OFDM_FEC_DATA_BYTES] = {0};
    static float transmitted[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    static float captured[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    ofdm_frame_t frame = {0};
    ofdm_frame_header_t parsed_header = {0};
    ofdm_phy_frame_metrics_t metrics = {0};
    uint16_t corrected_symbols = UINT16_MAX;

    for (size_t index = 0U; index < sizeof(message); ++index) {
        message[index] = (uint8_t)('a' + (index % 26U));
    }
    assert(OFDM_FRAME_OK == ofdm_frame_build(
                                message, sizeof(message),
                                TEST_CHANNEL_SESSION_ID, 0U, &frame));
    assert(OFDM_FRAME_OK ==
           ofdm_frame_header_serialize(&frame.header, header_wire));
    assert(OFDM_FEC_OK == ofdm_fec_init());
    assert(OFDM_PHY_OK == ofdm_phy_init());
    assert(OFDM_FEC_OK == ofdm_fec_encode(
                              frame.payload, encoded,
                              frame.header.session_id,
                              frame.header.frame_index));
    assert(OFDM_PHY_OK == ofdm_phy_modulate_frame(
                              header_wire, encoded, transmitted));

    apply_test_channel(transmitted, captured, OFDM_FRAME_SAMPLE_COUNT,
                       TEST_NOISE_AMPLITUDE);
    assert(OFDM_PHY_OK == ofdm_phy_demodulate_frame(
                              captured, decoded_header, received, &metrics));
    assert(0 == memcmp(header_wire, decoded_header, sizeof(header_wire)));
    assert(OFDM_FRAME_OK ==
           ofdm_frame_header_parse(decoded_header, &parsed_header));
    assert(OFDM_FEC_OK == ofdm_fec_decode(
                              received, decoded_payload,
                              parsed_header.session_id,
                              parsed_header.frame_index,
                              &corrected_symbols));
    assert(0 == memcmp(frame.payload, decoded_payload,
                       sizeof(decoded_payload)));
    assert(isfinite(metrics.header_evm_db));
    assert(isfinite(metrics.payload_evm_db));

    uint32_t noise_state = TEST_LCG_SEED;
    for (size_t index = 0U; index < OFDM_FRAME_SAMPLE_COUNT; ++index) {
        noise_state = noise_state * TEST_LCG_MULTIPLIER +
                      TEST_LCG_INCREMENT;
        const int32_t noise_code =
            (int32_t)((noise_state >> 24U) & UINT32_C(0xFF)) - 128;
        captured[index] = (float)noise_code / TEST_NOISE_DIVISOR *
                          TEST_REJECT_NOISE_AMPLITUDE;
    }
    assert(OFDM_PHY_INVALID_SIGNAL == ofdm_phy_demodulate_frame(
                                          captured, decoded_header,
                                          received, &metrics));
    for (size_t index = 0U; index < sizeof(decoded_header); ++index) {
        assert(0U == decoded_header[index]);
    }
    for (size_t index = 0U; index < sizeof(received); ++index) {
        assert(0U == received[index]);
    }

    ofdm_phy_deinit();
    ofdm_fec_deinit();
}

static void test_frame_with_moderate_noise(void)
{
    static uint8_t header[OFDM_FRAME_HEADER_BYTES] = {0};
    static uint8_t payload[OFDM_CODED_PAYLOAD_BYTES] = {0};
    static uint8_t decoded_header[OFDM_FRAME_HEADER_BYTES] = {0};
    static uint8_t decoded_payload[OFDM_CODED_PAYLOAD_BYTES] = {0};
    static float transmitted[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    static float captured[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    ofdm_phy_frame_metrics_t metrics = {0};

    fill_payload(header, sizeof(header));
    fill_payload(payload, sizeof(payload));
    assert(OFDM_PHY_OK == ofdm_phy_init());
    assert(OFDM_PHY_OK == ofdm_phy_modulate_frame(
                              header, payload, transmitted));
    apply_test_channel(transmitted, captured, OFDM_FRAME_SAMPLE_COUNT,
                       TEST_MODERATE_NOISE_AMPLITUDE);

    const ofdm_phy_result_t result = ofdm_phy_demodulate_frame(
        captured, decoded_header, decoded_payload, &metrics);
    if (OFDM_PHY_OK != result) {
        fprintf(stderr, "moderate noise rejected: sc=%.4f lts=%.4f\n",
                metrics.sc_score, metrics.lts_score);
    }
    assert(OFDM_PHY_OK == result);
    assert(0 == memcmp(header, decoded_header, sizeof(header)));
    assert(0 == memcmp(payload, decoded_payload, sizeof(payload)));
    assert(0.90F < metrics.sc_score);
    assert(0.90F < metrics.lts_score);
    assert(0.99F > metrics.sc_score || 0.99F > metrics.lts_score);
    ofdm_phy_deinit();
}

static void test_header_survives_contiguous_carrier_band_corruption(void)
{
    static uint8_t header[OFDM_FRAME_HEADER_BYTES] = {0};
    static uint8_t payload[OFDM_CODED_PAYLOAD_BYTES] = {0};
    static uint8_t decoded_header[OFDM_FRAME_HEADER_BYTES] = {0};
    static uint8_t decoded_payload[OFDM_CODED_PAYLOAD_BYTES] = {0};
    static float samples[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    ofdm_phy_frame_metrics_t metrics = {0};

    fill_payload(header, sizeof(header));
    fill_payload(payload, sizeof(payload));
    assert(OFDM_PHY_OK == ofdm_phy_init());
    assert(OFDM_PHY_OK == ofdm_phy_modulate_frame(
                              header, payload, samples));
    corrupt_header_carrier_band(samples);

    assert(OFDM_PHY_OK == ofdm_phy_demodulate_frame(
                              samples, decoded_header, decoded_payload,
                              &metrics));
    assert(0 == memcmp(header, decoded_header, sizeof(header)));
    assert(0 == memcmp(payload, decoded_payload, sizeof(payload)));
    ofdm_phy_deinit();
}

static void test_first_lts_is_warmup(void)
{
    static uint8_t header[OFDM_FRAME_HEADER_BYTES] = {0};
    static uint8_t payload[OFDM_CODED_PAYLOAD_BYTES] = {0};
    static uint8_t decoded_header[OFDM_FRAME_HEADER_BYTES] = {0};
    static uint8_t decoded_payload[OFDM_CODED_PAYLOAD_BYTES] = {0};
    static float samples[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    ofdm_phy_frame_metrics_t metrics = {0};

    fill_payload(header, sizeof(header));
    fill_payload(payload, sizeof(payload));
    assert(OFDM_PHY_OK == ofdm_phy_init());
    assert(OFDM_PHY_OK == ofdm_phy_modulate_frame(
                              header, payload, samples));
    memset(&samples[OFDM_FRAME_LTS_OFFSET], 0,
           OFDM_SYMBOL_SAMPLES * sizeof(samples[0]));

    assert(OFDM_PHY_OK == ofdm_phy_demodulate_frame(
                              samples, decoded_header, decoded_payload,
                              &metrics));
    assert(0 == memcmp(header, decoded_header, sizeof(header)));
    assert(0 == memcmp(payload, decoded_payload, sizeof(payload)));
    ofdm_phy_deinit();
}

static void test_nonfinite_frame_rejection(void)
{
    static float samples[OFDM_FRAME_SAMPLE_COUNT] = {0.0F};
    uint8_t header[OFDM_FRAME_HEADER_BYTES] = {0};
    uint8_t payload[OFDM_CODED_PAYLOAD_BYTES] = {0};
    uint8_t decoded_header[OFDM_FRAME_HEADER_BYTES] = {0};
    uint8_t decoded_payload[OFDM_CODED_PAYLOAD_BYTES] = {0};
    ofdm_phy_frame_metrics_t metrics = {0};

    fill_payload(header, sizeof(header));
    fill_payload(payload, sizeof(payload));
    assert(OFDM_PHY_OK == ofdm_phy_init());
    assert(OFDM_PHY_OK == ofdm_phy_modulate_frame(header, payload, samples));
    samples[OFDM_FRAME_SC_OFFSET + OFDM_CP_SAMPLES] = NAN;
    assert(OFDM_PHY_INVALID_SIGNAL == ofdm_phy_demodulate_frame(
                                          samples, decoded_header,
                                          decoded_payload, &metrics));
    ofdm_phy_deinit();
}

int main(void)
{
    test_carrier_layout();
    test_payload_round_trip();
    test_fec_phy_round_trip();
    test_complete_frame_round_trip();
    test_frame_through_short_multipath_channel();
    test_frame_with_moderate_noise();
    test_header_survives_contiguous_carrier_band_corruption();
    test_first_lts_is_warmup();
    test_nonfinite_frame_rejection();
    puts("ofdm_phy_test: PASS");
    return 0;
}
