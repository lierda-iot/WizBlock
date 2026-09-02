#include "ofdm_phy.h"

#include <math.h>
#include <string.h>

#include <kiss_fft.h>

#define OFDM_QPSK_SCALE 0.70710678118654752440F
#define OFDM_IFFT_SCALE (1.0F / (float)OFDM_FFT_SIZE)
#define OFDM_CHANNEL_POWER_MIN 1.0e-12F
#define OFDM_EVM_POWER_FLOOR 1.0e-20F
#define OFDM_PI 3.14159265358979323846F
#define OFDM_BIN_FREQUENCY_HZ(bin) \
    ((float)(bin) * (float)OFDM_SAMPLE_RATE_HZ / (float)OFDM_FFT_SIZE)
#define OFDM_CHIRP_START_CARRIER_HZ \
    OFDM_BIN_FREQUENCY_HZ(OFDM_FIRST_CARRIER_BIN)
#define OFDM_CHIRP_END_CARRIER_HZ \
    OFDM_BIN_FREQUENCY_HZ(OFDM_LAST_CARRIER_BIN)
#define OFDM_SC_ENERGY_MIN 1.0e-12F
#define OFDM_HEADER_SCALE 0.35F
#define OFDM_TRAINING_SCALE 0.5F
#define OFDM_FRAME_SYMBOL_GAIN 3.0F
#define OFDM_UNIT_GAIN 1.0F
#define OFDM_LTS_ESTIMATE_SYMBOL_COUNT 2U
#define OFDM_PHY_RESPONSE_USABLE_MARGIN_DB 18.0F
#define OFDM_LTS_FIRST_ESTIMATE_SYMBOL \
    (OFDM_LTS_SYMBOL_COUNT - OFDM_LTS_ESTIMATE_SYMBOL_COUNT)

static const uint16_t s_data_bins[OFDM_DATA_CARRIER_COUNT] = {
    32U, 33U, 35U, 36U, 37U, 38U, 39U, 40U,
    41U, 43U, 44U, 45U, 46U, 47U, 48U, 49U,
    51U, 52U, 53U, 54U, 55U, 56U, 57U, 59U,
};
static const uint16_t s_pilot_bins[OFDM_PILOT_COUNT] = {
    OFDM_NORMAL_PILOT_BIN_0,
    OFDM_NORMAL_PILOT_BIN_1,
    OFDM_NORMAL_PILOT_BIN_2,
    OFDM_NORMAL_PILOT_BIN_3,
};
static const int8_t s_pilot_base[OFDM_PILOT_COUNT] = {
    1, 1, 1, -1,
};

static kiss_fft_cfg s_forward_fft = NULL;
static kiss_fft_cfg s_inverse_fft = NULL;
static kiss_fft_cpx s_fft_input[OFDM_FFT_SIZE];
static kiss_fft_cpx s_fft_output[OFDM_FFT_SIZE];
static kiss_fft_cpx
    s_lts_received[OFDM_LTS_ESTIMATE_SYMBOL_COUNT][OFDM_FFT_SIZE];
static float s_channel_real[OFDM_FFT_SIZE];
static float s_channel_imaginary[OFDM_FFT_SIZE];

_Static_assert(OFDM_PAYLOAD_BITS % 2U == 0U,
               "QPSK payload must contain whole bit pairs");
_Static_assert(sizeof(s_data_bins) / sizeof(s_data_bins[0]) ==
                   OFDM_DATA_CARRIER_COUNT,
               "data carrier table size mismatch");
_Static_assert(OFDM_DATA_CARRIER_COUNT ==
                   OFDM_LAST_CARRIER_BIN - OFDM_FIRST_CARRIER_BIN + 1U -
                       OFDM_PILOT_COUNT,
               "active carrier range must match data and pilot counts");
_Static_assert(0U == (OFDM_NORMAL_PILOT_BIN_0 & 1U) &&
                   0U == (OFDM_NORMAL_PILOT_BIN_1 & 1U) &&
                   0U == (OFDM_NORMAL_PILOT_BIN_2 & 1U) &&
                   0U == (OFDM_NORMAL_PILOT_BIN_3 & 1U),
               "pilot bins must preserve the repeated short training symbol");
_Static_assert(OFDM_LTS_ESTIMATE_SYMBOL_COUNT < OFDM_LTS_SYMBOL_COUNT,
               "LTS estimate must exclude at least one warm-up symbol");

static uint8_t read_payload_bit(const uint8_t *payload, size_t bit_index)
{
    const size_t byte_index = bit_index / 8U;
    const uint8_t shift = (uint8_t)(7U - (bit_index % 8U));

    return (uint8_t)((payload[byte_index] >> shift) & 1U);
}

static void write_payload_bit(uint8_t *payload,
                              size_t bit_index,
                              uint8_t value)
{
    const size_t byte_index = bit_index / 8U;
    const uint8_t shift = (uint8_t)(7U - (bit_index % 8U));
    const uint8_t mask = (uint8_t)(UINT8_C(1) << shift);

    if (0U != value) {
        payload[byte_index] |= mask;
    }
}

static float get_pilot_value(size_t symbol_index, size_t pilot_index)
{
    const int8_t symbol_polarity = 0U == (symbol_index & 1U) ? 1 : -1;

    return (float)(s_pilot_base[pilot_index] * symbol_polarity);
}

static float get_lts_reference(uint16_t bin)
{
    if (ofdm_phy_is_pilot_bin(bin)) {
        return OFDM_TRAINING_SCALE;
    }
    return 0U == ((bin - OFDM_FIRST_CARRIER_BIN) & 1U)
               ? OFDM_TRAINING_SCALE
               : -OFDM_TRAINING_SCALE;
}

static uint8_t read_bit(const uint8_t *data, size_t bit_index)
{
    return (uint8_t)((data[bit_index / 8U] >>
                      (7U - (bit_index % 8U))) & 1U);
}

static void write_bit(uint8_t *data, size_t bit_index, uint8_t value)
{
    if (0U != value) {
        data[bit_index / 8U] |= (uint8_t)(
            UINT8_C(1) << (7U - (bit_index % 8U)));
    }
}

static void set_hermitian_bin(uint16_t bin, float real, float imaginary)
{
    s_fft_input[bin].r = real;
    s_fft_input[bin].i = imaginary;
    s_fft_input[OFDM_FFT_SIZE - bin].r = real;
    s_fft_input[OFDM_FFT_SIZE - bin].i = -imaginary;
}

static void map_payload_symbol(const uint8_t *payload, size_t symbol_index)
{
    memset(s_fft_input, 0, sizeof(s_fft_input));

    for (size_t carrier_index = 0U;
         carrier_index < OFDM_DATA_CARRIER_COUNT; ++carrier_index) {
        const size_t qpsk_index =
            symbol_index * OFDM_DATA_CARRIER_COUNT + carrier_index;
        uint8_t in_phase_bit = 0U;
        uint8_t quadrature_bit = 0U;

        if (qpsk_index < OFDM_PAYLOAD_QPSK_SYMBOLS) {
            in_phase_bit = read_payload_bit(payload, qpsk_index * 2U);
            quadrature_bit = read_payload_bit(payload,
                                              qpsk_index * 2U + 1U);
        }
        const float real = 0U == in_phase_bit
                               ? OFDM_QPSK_SCALE
                               : -OFDM_QPSK_SCALE;
        const float imaginary = 0U == quadrature_bit
                                    ? OFDM_QPSK_SCALE
                                    : -OFDM_QPSK_SCALE;
        set_hermitian_bin(s_data_bins[carrier_index], real, imaginary);
    }

    for (size_t pilot_index = 0U;
         pilot_index < OFDM_PILOT_COUNT; ++pilot_index) {
        set_hermitian_bin(s_pilot_bins[pilot_index],
                          get_pilot_value(symbol_index, pilot_index),
                          0.0F);
    }
}

static void map_training_symbol(bool long_training)
{
    memset(s_fft_input, 0, sizeof(s_fft_input));
    for (uint16_t bin = OFDM_FIRST_CARRIER_BIN;
         bin <= OFDM_LAST_CARRIER_BIN; ++bin) {
        if (!long_training && 0U != (bin & 1U)) {
            continue;
        }
        if (ofdm_phy_is_pilot_bin(bin)) {
            continue;
        }
        const uint8_t polarity = (uint8_t)(
            0U == ((bin - OFDM_FIRST_CARRIER_BIN) & 1U) ? 1U : 0U);
        const float real = 0U != polarity ? 1.0F : -1.0F;
        set_hermitian_bin(bin, real * OFDM_TRAINING_SCALE, 0.0F);
    }
    for (size_t pilot_index = 0U;
         pilot_index < OFDM_PILOT_COUNT; ++pilot_index) {
        set_hermitian_bin(s_pilot_bins[pilot_index],
                          OFDM_TRAINING_SCALE, 0.0F);
    }
}

static void map_header_symbol(const uint8_t *header_wire,
                              size_t symbol_index)
{
    memset(s_fft_input, 0, sizeof(s_fft_input));
    for (size_t carrier_index = 0U;
         carrier_index < OFDM_DATA_CARRIER_COUNT; ++carrier_index) {
        const size_t coded_bit =
            symbol_index * OFDM_DATA_CARRIER_COUNT + carrier_index;
        uint8_t value = 0U;
        if (coded_bit < OFDM_HEADER_CODED_BITS) {
            const size_t header_bit = coded_bit % OFDM_HEADER_BITS;
            value = read_bit(header_wire, header_bit);
        }
        const float real = 0U == value ? OFDM_HEADER_SCALE :
                                         -OFDM_HEADER_SCALE;
        set_hermitian_bin(s_data_bins[carrier_index], real, 0.0F);
    }
    for (size_t pilot_index = 0U;
         pilot_index < OFDM_PILOT_COUNT; ++pilot_index) {
        set_hermitian_bin(s_pilot_bins[pilot_index],
                          get_pilot_value(symbol_index, pilot_index), 0.0F);
    }
}

void ofdm_phy_fill_chirp(float samples[OFDM_CHIRP_SAMPLES])
{
    if (NULL == samples) {
        return;
    }
    for (size_t index = 0U; index < OFDM_CHIRP_SAMPLES; ++index) {
        const float progress = (float)index /
                               (float)(OFDM_CHIRP_SAMPLES - 1U);
        const float frequency = OFDM_CHIRP_START_CARRIER_HZ +
                                progress * (OFDM_CHIRP_END_CARRIER_HZ -
                                            OFDM_CHIRP_START_CARRIER_HZ);
        const float phase = 2.0F * OFDM_PI *
                            (OFDM_CHIRP_START_CARRIER_HZ *
                                 (float)index /
                                 (float)OFDM_SAMPLE_RATE_HZ +
                             0.5F * (frequency - OFDM_CHIRP_START_CARRIER_HZ) *
                                 (float)index /
                                 (float)OFDM_SAMPLE_RATE_HZ);
        samples[index] = 0.25F * sinf(phase);
    }
}

static void write_ifft_symbol(float *samples,
                              size_t offset,
                              float output_gain)
{
    kiss_fft(s_inverse_fft, s_fft_input, s_fft_output);
    for (size_t sample_index = 0U;
         sample_index < OFDM_FFT_SIZE; ++sample_index) {
        samples[offset + OFDM_CP_SAMPLES + sample_index] =
            s_fft_output[sample_index].r * OFDM_IFFT_SCALE * output_gain;
    }
    for (size_t cp_index = 0U; cp_index < OFDM_CP_SAMPLES; ++cp_index) {
        samples[offset + cp_index] = samples[
            offset + OFDM_FFT_SIZE + cp_index];
    }
}

static bool load_symbol_at(const float *samples,
                           size_t offset,
                           bool reject_nonfinite)
{
    for (size_t sample_index = 0U;
         sample_index < OFDM_FFT_SIZE; ++sample_index) {
        const float sample = samples[offset + OFDM_CP_SAMPLES + sample_index];
        if (reject_nonfinite && !isfinite(sample)) {
            return false;
        }
        s_fft_input[sample_index].r = sample;
        s_fft_input[sample_index].i = 0.0F;
    }
    return true;
}

static float calculate_training_score(const float *samples,
                                      size_t first_offset,
                                      size_t second_offset)
{
    float numerator = 0.0F;
    float first_power = 0.0F;
    float second_power = 0.0F;
    for (size_t index = 0U; index < OFDM_FFT_SIZE; ++index) {
        const float first = samples[first_offset + OFDM_CP_SAMPLES + index];
        const float second = samples[second_offset + OFDM_CP_SAMPLES + index];
        numerator += first * second;
        first_power += first * first;
        second_power += second * second;
    }
    const float denominator = sqrtf(first_power * second_power);
    return denominator > OFDM_SC_ENERGY_MIN ? numerator / denominator : 0.0F;
}

static float calculate_sc_score(const float *samples)
{
    const size_t body = OFDM_FRAME_SC_OFFSET + OFDM_CP_SAMPLES;
    float numerator = 0.0F;
    float first_power = 0.0F;
    float second_power = 0.0F;
    for (size_t index = 0U; index < OFDM_FFT_SIZE / 2U; ++index) {
        const float first = samples[body + index];
        const float second = samples[body + OFDM_FFT_SIZE / 2U + index];
        numerator += first * second;
        first_power += first * first;
        second_power += second * second;
    }
    const float denominator = sqrtf(first_power * second_power);
    return denominator > OFDM_SC_ENERGY_MIN ? numerator / denominator : 0.0F;
}

bool ofdm_phy_measure_training(
    const float samples[OFDM_FRAME_SAMPLE_COUNT],
    float *sc_score,
    float *lts_score)
{
    if (NULL == samples || NULL == sc_score || NULL == lts_score) {
        return false;
    }

    *sc_score = calculate_sc_score(samples);
    const size_t first_estimate_offset = OFDM_FRAME_LTS_OFFSET +
        OFDM_LTS_FIRST_ESTIMATE_SYMBOL * OFDM_SYMBOL_SAMPLES;
    *lts_score = calculate_training_score(
        samples, first_estimate_offset,
        first_estimate_offset + OFDM_SYMBOL_SAMPLES);
    return isfinite(*sc_score) && isfinite(*lts_score);
}

static bool estimate_lts_channel(const float *samples,
                                 float *score,
                                 ofdm_phy_frame_metrics_t *metrics)
{
    if (NULL == samples || NULL == score || NULL == metrics) {
        return false;
    }
    for (size_t symbol_index = 0U;
         symbol_index < OFDM_LTS_ESTIMATE_SYMBOL_COUNT; ++symbol_index) {
        const size_t offset = OFDM_FRAME_LTS_OFFSET +
            (OFDM_LTS_FIRST_ESTIMATE_SYMBOL + symbol_index) *
                OFDM_SYMBOL_SAMPLES;
        if (!load_symbol_at(samples, offset, true)) {
            return false;
        }
        kiss_fft(s_forward_fft, s_fft_input,
                 s_lts_received[symbol_index]);
    }

    const size_t first_estimate_offset = OFDM_FRAME_LTS_OFFSET +
        OFDM_LTS_FIRST_ESTIMATE_SYMBOL * OFDM_SYMBOL_SAMPLES;
    *score = calculate_training_score(
        samples, first_estimate_offset,
        first_estimate_offset + OFDM_SYMBOL_SAMPLES);
    if (!isfinite(*score)) {
        return false;
    }
    memset(s_channel_real, 0, sizeof(s_channel_real));
    memset(s_channel_imaginary, 0, sizeof(s_channel_imaginary));
    for (uint16_t bin = OFDM_FIRST_CARRIER_BIN;
         bin <= OFDM_LAST_CARRIER_BIN; ++bin) {
        const float reference = get_lts_reference(bin);
        const float received_real =
            0.5F * (s_lts_received[0][bin].r + s_lts_received[1][bin].r);
        const float received_imaginary =
            0.5F * (s_lts_received[0][bin].i + s_lts_received[1][bin].i);
        s_channel_real[bin] = received_real / reference;
        s_channel_imaginary[bin] = received_imaginary / reference;
        if (OFDM_CHANNEL_POWER_MIN >=
            s_channel_real[bin] * s_channel_real[bin] +
                s_channel_imaginary[bin] * s_channel_imaginary[bin]) {
            return false;
        }
    }

    float maximum_db = OFDM_PHY_RESPONSE_INVALID_DB;
    float minimum_db = OFDM_PHY_RESPONSE_INVALID_DB;
    float magnitudes[OFDM_LAST_CARRIER_BIN - OFDM_FIRST_CARRIER_BIN + 1U] = {
        0.0F};
    for (uint16_t bin = OFDM_FIRST_CARRIER_BIN;
         bin <= OFDM_LAST_CARRIER_BIN; ++bin) {
        const float magnitude = sqrtf(
            s_channel_real[bin] * s_channel_real[bin] +
            s_channel_imaginary[bin] * s_channel_imaginary[bin]);
        const size_t magnitude_index = bin - OFDM_FIRST_CARRIER_BIN;
        magnitudes[magnitude_index] = magnitude;
        const float magnitude_db =
            20.0F * log10f(fmaxf(magnitude, OFDM_CHANNEL_POWER_MIN));
        if (magnitude_db > maximum_db) {
            maximum_db = magnitude_db;
        }
        if (OFDM_PHY_RESPONSE_INVALID_DB == minimum_db ||
            magnitude_db < minimum_db) {
            minimum_db = magnitude_db;
        }
    }
    metrics->channel_min_db = minimum_db;
    metrics->channel_max_db = maximum_db;
    metrics->channel_spread_db = maximum_db - minimum_db;
    metrics->channel_usable_carriers = 0U;
    for (size_t group_index = 0U;
         group_index < OFDM_PHY_RESPONSE_GROUP_COUNT; ++group_index) {
        metrics->channel_group_db[group_index] =
            OFDM_PHY_RESPONSE_INVALID_DB;
        uint16_t group_first = 0U;
        uint16_t group_last = 0U;
        if (!ofdm_phy_get_response_group_range(group_index, &group_first,
                                               &group_last)) {
            continue;
        }
        float group_power = 0.0F;
        uint16_t group_count = 0U;
        for (uint16_t bin = group_first; bin <= group_last; ++bin) {
            group_power += magnitudes[bin - OFDM_FIRST_CARRIER_BIN] *
                           magnitudes[bin - OFDM_FIRST_CARRIER_BIN];
            ++group_count;
            const float bin_db = 20.0F * log10f(fmaxf(
                magnitudes[bin - OFDM_FIRST_CARRIER_BIN],
                OFDM_CHANNEL_POWER_MIN));
            if (maximum_db - bin_db <= OFDM_PHY_RESPONSE_USABLE_MARGIN_DB) {
                if (UINT8_MAX > metrics->channel_usable_carriers) {
                    ++metrics->channel_usable_carriers;
                }
            }
        }
        if (0U < group_count) {
            const float group_db = 10.0F * log10f(
                fmaxf(group_power / (float)group_count,
                      OFDM_CHANNEL_POWER_MIN));
            metrics->channel_group_db[group_index] = group_db - maximum_db;
        }
    }
    return true;
}

static void equalize_symbol(const kiss_fft_cpx *received,
                            float channel_real,
                            float channel_imaginary,
                            float *equalized_real,
                            float *equalized_imaginary)
{
    const float channel_power = channel_real * channel_real +
                                channel_imaginary * channel_imaginary;

    *equalized_real = (received->r * channel_real +
                       received->i * channel_imaginary) / channel_power;
    *equalized_imaginary = (received->i * channel_real -
                            received->r * channel_imaginary) / channel_power;
}

static bool estimate_common_phase_from_lts(size_t symbol_index,
                                           float *phase_real,
                                           float *phase_imaginary)
{
    float gain_real = 0.0F;
    float gain_imaginary = 0.0F;

    if (NULL == phase_real || NULL == phase_imaginary) {
        return false;
    }

    for (size_t pilot_index = 0U;
         pilot_index < OFDM_PILOT_COUNT; ++pilot_index) {
        const uint16_t bin = s_pilot_bins[pilot_index];
        float equalized_real = 0.0F;
        float equalized_imaginary = 0.0F;
        equalize_symbol(&s_fft_output[bin], s_channel_real[bin],
                        s_channel_imaginary[bin], &equalized_real,
                        &equalized_imaginary);
        const float pilot = get_pilot_value(symbol_index, pilot_index);
        gain_real += equalized_real * pilot;
        gain_imaginary += equalized_imaginary * pilot;
    }

    gain_real /= (float)OFDM_PILOT_COUNT;
    gain_imaginary /= (float)OFDM_PILOT_COUNT;
    const float gain_power = gain_real * gain_real +
                             gain_imaginary * gain_imaginary;
    if (OFDM_CHANNEL_POWER_MIN >= gain_power) {
        return false;
    }

    const float gain_magnitude = sqrtf(gain_power);
    *phase_real = gain_real / gain_magnitude;
    *phase_imaginary = gain_imaginary / gain_magnitude;
    return true;
}

static void remove_common_phase(float input_real,
                                float input_imaginary,
                                float phase_real,
                                float phase_imaginary,
                                float *output_real,
                                float *output_imaginary)
{
    *output_real = input_real * phase_real +
                   input_imaginary * phase_imaginary;
    *output_imaginary = input_imaginary * phase_real -
                        input_real * phase_imaginary;
}

static bool demodulate_header_symbol(uint8_t *symbol_bits,
                                     size_t symbol_index,
                                     float *error_power,
                                     float *reference_power)
{
    if (NULL == symbol_bits || NULL == error_power ||
        NULL == reference_power) {
        return false;
    }

    float phase_real = 0.0F;
    float phase_imaginary = 0.0F;
    if (!estimate_common_phase_from_lts(symbol_index, &phase_real,
                                        &phase_imaginary)) {
        return false;
    }

    for (size_t carrier_index = 0U;
         carrier_index < OFDM_DATA_CARRIER_COUNT; ++carrier_index) {
        const size_t coded_bit =
            symbol_index * OFDM_DATA_CARRIER_COUNT + carrier_index;
        if (coded_bit >= OFDM_HEADER_CODED_BITS) {
            continue;
        }
        const uint16_t bin = s_data_bins[carrier_index];
        float equalized_real = 0.0F;
        float equalized_imaginary = 0.0F;
        float real = 0.0F;
        float imaginary = 0.0F;
        equalize_symbol(&s_fft_output[bin], s_channel_real[bin],
                        s_channel_imaginary[bin], &equalized_real,
                        &equalized_imaginary);
        remove_common_phase(equalized_real, equalized_imaginary,
                            phase_real, phase_imaginary, &real, &imaginary);
        symbol_bits[coded_bit] = real < 0.0F ? 1U : 0U;

        const float expected = 0U == symbol_bits[coded_bit]
                                   ? OFDM_HEADER_SCALE
                                   : -OFDM_HEADER_SCALE;
        const float error = real - expected;
        *error_power += error * error + imaginary * imaginary;
        *reference_power += expected * expected;
    }
    return true;
}

static bool load_time_symbol(const float *samples, size_t symbol_index)
{
    const size_t base = symbol_index * OFDM_SYMBOL_SAMPLES +
                        OFDM_CP_SAMPLES;

    for (size_t sample_index = 0U;
         sample_index < OFDM_FFT_SIZE; ++sample_index) {
        const float sample = samples[base + sample_index];
        if (!isfinite(sample)) {
            return false;
        }
        s_fft_input[sample_index].r = sample;
        s_fft_input[sample_index].i = 0.0F;
    }
    return true;
}

static bool estimate_flat_channel(size_t symbol_index,
                                  float *channel_real,
                                  float *channel_imaginary)
{
    float real = 0.0F;
    float imaginary = 0.0F;

    for (size_t pilot_index = 0U;
         pilot_index < OFDM_PILOT_COUNT; ++pilot_index) {
        const float pilot = get_pilot_value(symbol_index, pilot_index);
        const kiss_fft_cpx received = s_fft_output[s_pilot_bins[pilot_index]];
        real += received.r * pilot;
        imaginary += received.i * pilot;
    }
    real /= (float)OFDM_PILOT_COUNT;
    imaginary /= (float)OFDM_PILOT_COUNT;
    if (OFDM_CHANNEL_POWER_MIN >= real * real + imaginary * imaginary) {
        return false;
    }
    *channel_real = real;
    *channel_imaginary = imaginary;
    return true;
}

static ofdm_phy_result_t demodulate_payload_internal(
    const float *samples,
    uint8_t *payload,
    float *evm_db,
    bool use_lts_channel)
{
    if (NULL == samples || NULL == payload) {
        return OFDM_PHY_INVALID_ARGUMENT;
    }

    float error_power = 0.0F;
    float reference_power = 0.0F;
    memset(payload, 0, OFDM_CODED_PAYLOAD_BYTES);
    for (size_t symbol_index = 0U;
         symbol_index < OFDM_PAYLOAD_SYMBOL_COUNT; ++symbol_index) {
        if (!load_time_symbol(samples, symbol_index)) {
            memset(payload, 0, OFDM_CODED_PAYLOAD_BYTES);
            return OFDM_PHY_INVALID_SIGNAL;
        }
        kiss_fft(s_forward_fft, s_fft_input, s_fft_output);

        float channel_real = 0.0F;
        float channel_imaginary = 0.0F;
        float phase_real = 1.0F;
        float phase_imaginary = 0.0F;
        if (use_lts_channel) {
            if (!estimate_common_phase_from_lts(symbol_index, &phase_real,
                                                &phase_imaginary)) {
                memset(payload, 0, OFDM_CODED_PAYLOAD_BYTES);
                return OFDM_PHY_INVALID_SIGNAL;
            }
        } else if (!estimate_flat_channel(symbol_index, &channel_real,
                                          &channel_imaginary)) {
            memset(payload, 0, OFDM_CODED_PAYLOAD_BYTES);
            return OFDM_PHY_INVALID_SIGNAL;
        }

        for (size_t carrier_index = 0U;
             carrier_index < OFDM_DATA_CARRIER_COUNT; ++carrier_index) {
            const size_t qpsk_index =
                symbol_index * OFDM_DATA_CARRIER_COUNT + carrier_index;
            if (qpsk_index >= OFDM_PAYLOAD_QPSK_SYMBOLS) {
                break;
            }

            const uint16_t bin = s_data_bins[carrier_index];
            float equalized_real = 0.0F;
            float equalized_imaginary = 0.0F;
            equalize_symbol(
                &s_fft_output[bin],
                use_lts_channel ? s_channel_real[bin] : channel_real,
                use_lts_channel ? s_channel_imaginary[bin]
                                : channel_imaginary,
                &equalized_real, &equalized_imaginary);

            float real = equalized_real;
            float imaginary = equalized_imaginary;
            if (use_lts_channel) {
                remove_common_phase(equalized_real, equalized_imaginary,
                                    phase_real, phase_imaginary, &real,
                                    &imaginary);
            }

            const uint8_t in_phase_bit = real < 0.0F ? 1U : 0U;
            const uint8_t quadrature_bit = imaginary < 0.0F ? 1U : 0U;
            write_payload_bit(payload, qpsk_index * 2U, in_phase_bit);
            write_payload_bit(payload, qpsk_index * 2U + 1U,
                              quadrature_bit);

            const float ideal_real = 0U == in_phase_bit
                                         ? OFDM_QPSK_SCALE
                                         : -OFDM_QPSK_SCALE;
            const float ideal_imaginary = 0U == quadrature_bit
                                              ? OFDM_QPSK_SCALE
                                              : -OFDM_QPSK_SCALE;
            const float error_real = real - ideal_real;
            const float error_imaginary = imaginary - ideal_imaginary;
            error_power += error_real * error_real +
                           error_imaginary * error_imaginary;
            reference_power += ideal_real * ideal_real +
                               ideal_imaginary * ideal_imaginary;
        }
    }

    if (NULL != evm_db) {
        float ratio = error_power / reference_power;
        ratio = ratio < OFDM_EVM_POWER_FLOOR
                    ? OFDM_EVM_POWER_FLOOR
                    : ratio;
        *evm_db = 10.0F * log10f(ratio);
    }
    return OFDM_PHY_OK;
}

ofdm_phy_result_t ofdm_phy_init(void)
{
    if (NULL != s_forward_fft && NULL != s_inverse_fft) {
        return OFDM_PHY_OK;
    }
    ofdm_phy_deinit();

    s_forward_fft = kiss_fft_alloc((int)OFDM_FFT_SIZE, 0, NULL, NULL);
    s_inverse_fft = kiss_fft_alloc((int)OFDM_FFT_SIZE, 1, NULL, NULL);
    if (NULL == s_forward_fft || NULL == s_inverse_fft) {
        ofdm_phy_deinit();
        return OFDM_PHY_INIT_FAILED;
    }
    memset(s_fft_input, 0, sizeof(s_fft_input));
    memset(s_fft_output, 0, sizeof(s_fft_output));
    return OFDM_PHY_OK;
}

void ofdm_phy_deinit(void)
{
    if (NULL != s_forward_fft) {
        kiss_fft_free(s_forward_fft);
        s_forward_fft = NULL;
    }
    if (NULL != s_inverse_fft) {
        kiss_fft_free(s_inverse_fft);
        s_inverse_fft = NULL;
    }
}

bool ofdm_phy_is_pilot_bin(uint16_t bin)
{
    for (size_t index = 0U; index < OFDM_PILOT_COUNT; ++index) {
        if (s_pilot_bins[index] == bin) {
            return true;
        }
    }
    return false;
}

uint16_t ofdm_phy_get_data_bin(size_t data_index)
{
    return data_index < OFDM_DATA_CARRIER_COUNT
               ? s_data_bins[data_index]
               : 0U;
}

bool ofdm_phy_get_response_group_range(size_t group_index,
                                       uint16_t *first_bin,
                                       uint16_t *last_bin)
{
    if (OFDM_PHY_RESPONSE_GROUP_COUNT <= group_index ||
        (NULL == first_bin && NULL == last_bin)) {
        return false;
    }
    const size_t active_count =
        OFDM_LAST_CARRIER_BIN - OFDM_FIRST_CARRIER_BIN + 1U;
    const uint16_t group_first = (uint16_t)(
        OFDM_FIRST_CARRIER_BIN +
        (group_index * active_count) / OFDM_PHY_RESPONSE_GROUP_COUNT);
    const uint16_t group_last = (uint16_t)(
        OFDM_FIRST_CARRIER_BIN +
        (((group_index + 1U) * active_count) /
         OFDM_PHY_RESPONSE_GROUP_COUNT) - 1U);
    if (NULL != first_bin) {
        *first_bin = group_first;
    }
    if (NULL != last_bin) {
        *last_bin = group_last;
    }
    return true;
}

static void modulate_payload_internal(
    const uint8_t payload[OFDM_CODED_PAYLOAD_BYTES],
    float samples[OFDM_PAYLOAD_SAMPLE_COUNT],
    float output_gain)
{
    for (size_t symbol_index = 0U;
         symbol_index < OFDM_PAYLOAD_SYMBOL_COUNT; ++symbol_index) {
        map_payload_symbol(payload, symbol_index);
        kiss_fft(s_inverse_fft, s_fft_input, s_fft_output);

        const size_t base = symbol_index * OFDM_SYMBOL_SAMPLES;
        for (size_t sample_index = 0U;
             sample_index < OFDM_FFT_SIZE; ++sample_index) {
            samples[base + OFDM_CP_SAMPLES + sample_index] =
                s_fft_output[sample_index].r * OFDM_IFFT_SCALE *
                output_gain;
        }
        for (size_t cp_index = 0U;
             cp_index < OFDM_CP_SAMPLES; ++cp_index) {
            samples[base + cp_index] = samples[
                base + OFDM_FFT_SIZE + cp_index];
        }
    }
}

ofdm_phy_result_t ofdm_phy_modulate_payload(
    const uint8_t payload[OFDM_CODED_PAYLOAD_BYTES],
    float samples[OFDM_PAYLOAD_SAMPLE_COUNT])
{
    if (NULL == payload || NULL == samples) {
        return OFDM_PHY_INVALID_ARGUMENT;
    }
    if (NULL == s_inverse_fft) {
        return OFDM_PHY_NOT_INITIALIZED;
    }
    modulate_payload_internal(payload, samples, OFDM_UNIT_GAIN);
    return OFDM_PHY_OK;
}

ofdm_phy_result_t ofdm_phy_demodulate_payload(
    const float samples[OFDM_PAYLOAD_SAMPLE_COUNT],
    uint8_t payload[OFDM_CODED_PAYLOAD_BYTES],
    float *evm_db)
{
    if (NULL == samples || NULL == payload) {
        return OFDM_PHY_INVALID_ARGUMENT;
    }
    if (NULL == s_forward_fft) {
        return OFDM_PHY_NOT_INITIALIZED;
    }

    return demodulate_payload_internal(samples, payload, evm_db, false);
}

ofdm_phy_result_t ofdm_phy_modulate_frame(
    const uint8_t header_wire[OFDM_FRAME_HEADER_BYTES],
    const uint8_t payload[OFDM_CODED_PAYLOAD_BYTES],
    float samples[OFDM_FRAME_SAMPLE_COUNT])
{
    if (NULL == header_wire || NULL == payload || NULL == samples) {
        return OFDM_PHY_INVALID_ARGUMENT;
    }
    if (NULL == s_inverse_fft) {
        return OFDM_PHY_NOT_INITIALIZED;
    }
    memset(samples, 0, sizeof(float) * OFDM_FRAME_SAMPLE_COUNT);
    ofdm_phy_fill_chirp(&samples[OFDM_FRAME_CHIRP_OFFSET]);

    map_training_symbol(false);
    write_ifft_symbol(samples, OFDM_FRAME_SC_OFFSET,
                      OFDM_FRAME_SYMBOL_GAIN);
    for (size_t symbol_index = 0U;
         symbol_index < OFDM_LTS_SYMBOL_COUNT; ++symbol_index) {
        map_training_symbol(true);
        write_ifft_symbol(
            samples, OFDM_FRAME_LTS_OFFSET +
                         symbol_index * OFDM_SYMBOL_SAMPLES,
            OFDM_FRAME_SYMBOL_GAIN);
    }

    for (size_t symbol_index = 0U;
         symbol_index < OFDM_HEADER_SYMBOL_COUNT; ++symbol_index) {
        map_header_symbol(header_wire, symbol_index);
        write_ifft_symbol(
            samples, OFDM_FRAME_HEADER_OFFSET +
                         symbol_index * OFDM_SYMBOL_SAMPLES,
            OFDM_FRAME_SYMBOL_GAIN);
    }

    modulate_payload_internal(
        payload, &samples[OFDM_FRAME_PAYLOAD_OFFSET],
        OFDM_FRAME_SYMBOL_GAIN);
    return OFDM_PHY_OK;
}

ofdm_phy_result_t ofdm_phy_demodulate_frame(
    const float samples[OFDM_FRAME_SAMPLE_COUNT],
    uint8_t header_wire[OFDM_FRAME_HEADER_BYTES],
    uint8_t payload[OFDM_CODED_PAYLOAD_BYTES],
    ofdm_phy_frame_metrics_t *metrics)
{
    if (NULL == samples || NULL == header_wire || NULL == payload ||
        NULL == metrics) {
        return OFDM_PHY_INVALID_ARGUMENT;
    }
    if (NULL == s_forward_fft) {
        return OFDM_PHY_NOT_INITIALIZED;
    }
    memset(header_wire, 0, OFDM_FRAME_HEADER_BYTES);
    memset(payload, 0, OFDM_CODED_PAYLOAD_BYTES);
    memset(metrics, 0, sizeof(*metrics));

    if (!ofdm_phy_measure_training(samples, &metrics->sc_score,
                                   &metrics->lts_score)) {
        return OFDM_PHY_INVALID_SIGNAL;
    }
    if (metrics->sc_score < OFDM_PHY_SC_MIN_SCORE ||
        metrics->lts_score < OFDM_PHY_LTS_MIN_SCORE) {
        return OFDM_PHY_INVALID_SIGNAL;
    }
    if (!estimate_lts_channel(samples, &metrics->lts_score, metrics)) {
        return OFDM_PHY_INVALID_SIGNAL;
    }
    if (!isfinite(metrics->lts_score) ||
        metrics->lts_score < OFDM_PHY_LTS_MIN_SCORE) {
        return OFDM_PHY_INVALID_SIGNAL;
    }

    uint8_t header_bits[OFDM_HEADER_CODED_BITS] = {0};
    float header_error = 0.0F;
    float header_reference = 0.0F;
    for (size_t symbol_index = 0U;
         symbol_index < OFDM_HEADER_SYMBOL_COUNT; ++symbol_index) {
        const size_t offset = OFDM_FRAME_HEADER_OFFSET +
                              symbol_index * OFDM_SYMBOL_SAMPLES;
        if (!load_symbol_at(samples, offset, true)) {
            return OFDM_PHY_INVALID_SIGNAL;
        }
        kiss_fft(s_forward_fft, s_fft_input, s_fft_output);
        if (!demodulate_header_symbol(header_bits, symbol_index,
                                      &header_error, &header_reference)) {
            return OFDM_PHY_INVALID_SIGNAL;
        }
    }
    for (size_t bit_index = 0U; bit_index < OFDM_HEADER_BITS; ++bit_index) {
        const uint8_t first = header_bits[bit_index];
        const uint8_t second = header_bits[OFDM_HEADER_BITS + bit_index];
        const uint8_t third = header_bits[
            (2U * OFDM_HEADER_BITS) + bit_index];
        const uint8_t value = (uint8_t)((first + second + third) >= 2U);
        write_bit(header_wire, bit_index, value);
    }
    metrics->header_evm_db = 10.0F * log10f(
        fmaxf(header_error / header_reference, OFDM_EVM_POWER_FLOOR));

    const ofdm_phy_result_t payload_result = demodulate_payload_internal(
        &samples[OFDM_FRAME_PAYLOAD_OFFSET], payload,
        &metrics->payload_evm_db, true);
    if (OFDM_PHY_OK != payload_result) {
        return payload_result;
    }
    return OFDM_PHY_OK;
}
