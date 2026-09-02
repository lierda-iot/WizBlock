#include "audio_spatial_spectrum_processor.h"

#include "esp_dsp.h"
#include "esp_log.h"

#include <math.h>
#include <stdbool.h>
#include <stddef.h>
#include <string.h>

static const char *TAG = "spatial_spectrum_dsp";

#define SPECTRUM_MIN_FREQUENCY_HZ 80.0f
#define SPECTRUM_MAX_FREQUENCY_HZ 8000.0f
#define SPECTRUM_FLOOR_DB         -72.0f
#define SPECTRUM_CEILING_DB       -18.0f
#define SPECTRUM_RMS_FLOOR_DB     -60.0f
#define SPECTRUM_NOISE_GATE_LEVEL 0.03f
#define SPECTRUM_ATTACK           0.65f
#define SPECTRUM_RELEASE          0.12f
#define SPECTRUM_PEAK_DECAY       0.025f
#define PCM_FULL_SCALE            32768.0f
#define HAMMING_COHERENT_GAIN     0.54f
#define TWO_PI                    6.28318530717958647692f
#define MAGNITUDE_EPSILON         1.0e-8f

static audio_spectrum_band_t s_bands[AUDIO_SPECTRUM_BAND_COUNT];
static float s_hamming_window[AUDIO_SPATIAL_FFT_SIZE] __attribute__((aligned(16)));
static float s_fft_buffer[AUDIO_SPATIAL_FFT_SIZE * 2U] __attribute__((aligned(16)));
static audio_spectrum_envelope_t s_combined_envelope;
static audio_spectrum_envelope_t s_mic1_envelope;
static audio_spectrum_envelope_t s_mic2_envelope;
static bool s_initialized;

static float calculate_mean(const int16_t *samples)
{
    int64_t sum = 0;
    for (size_t index = 0; index < AUDIO_SPATIAL_FFT_SIZE; index++) {
        sum += samples[index];
    }
    return (float)sum / (float)AUDIO_SPATIAL_FFT_SIZE;
}

static uint32_t calculate_rms(const int16_t *samples)
{
    uint64_t square_sum = 0U;
    for (size_t index = 0; index < AUDIO_SPATIAL_FFT_SIZE; index++) {
        const int32_t sample = samples[index];
        square_sum += (uint64_t)((int64_t)sample * sample);
    }
    return (uint32_t)sqrt((double)square_sum / (double)AUDIO_SPATIAL_FFT_SIZE);
}

static float calculate_energy_db(uint32_t mic1_rms, uint32_t mic2_rms)
{
    const double mean_square =
        ((double)mic1_rms * mic1_rms + (double)mic2_rms * mic2_rms) * 0.5;
    if (0.0 >= mean_square) {
        return -120.0f;
    }
    return (float)(10.0 * log10(mean_square + 1.0e-12));
}

static float calculate_rms_level(uint32_t rms)
{
    if (0U == rms) {
        return 0.0f;
    }
    const float rms_dbfs = 20.0f * log10f((float)rms / PCM_FULL_SCALE);
    return audio_spectrum_level_from_db(rms_dbfs,
                                        SPECTRUM_RMS_FLOOR_DB,
                                        SPECTRUM_CEILING_DB);
}

static float calculate_band_magnitude(const float *complex_spectrum,
                                      const audio_spectrum_band_t *band)
{
    double power_sum = 0.0;
    const uint16_t bin_count = (uint16_t)(band->last_bin - band->first_bin + 1U);

    for (uint16_t bin = band->first_bin; bin <= band->last_bin; bin++) {
        const float real = complex_spectrum[(size_t)bin * 2U];
        const float imaginary = complex_spectrum[(size_t)bin * 2U + 1U];
        power_sum += (double)real * real + (double)imaginary * imaginary;
    }

    const float rms_magnitude = (float)sqrt(power_sum / (double)bin_count);
    return rms_magnitude * (2.0f /
                            ((float)AUDIO_SPATIAL_FFT_SIZE * HAMMING_COHERENT_GAIN));
}

static float magnitude_to_level(float magnitude)
{
    const float magnitude_db = 20.0f * log10f(fmaxf(magnitude, MAGNITUDE_EPSILON));
    float level = audio_spectrum_level_from_db(magnitude_db,
                                               SPECTRUM_FLOOR_DB,
                                               SPECTRUM_CEILING_DB);
    if (SPECTRUM_NOISE_GATE_LEVEL > level) {
        level = 0.0f;
    }
    return level;
}

esp_err_t audio_spatial_spectrum_processor_init(uint32_t sample_rate_hz)
{
    if (s_initialized) {
        return ESP_OK;
    }

    if (!audio_spectrum_build_log_bands(sample_rate_hz,
                                        AUDIO_SPATIAL_FFT_SIZE,
                                        SPECTRUM_MIN_FREQUENCY_HZ,
                                        SPECTRUM_MAX_FREQUENCY_HZ,
                                        s_bands,
                                        AUDIO_SPECTRUM_BAND_COUNT)) {
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = dsps_fft2r_init_fc32(NULL, AUDIO_SPATIAL_FFT_SIZE);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "FFT init failed: %s", esp_err_to_name(ret));
        return ret;
    }

    for (size_t index = 0; index < AUDIO_SPATIAL_FFT_SIZE; index++) {
        s_hamming_window[index] = 0.54f -
                                  0.46f * cosf(TWO_PI * (float)index /
                                               (float)(AUDIO_SPATIAL_FFT_SIZE - 1U));
    }
    memset(&s_combined_envelope, 0, sizeof(s_combined_envelope));
    memset(&s_mic1_envelope, 0, sizeof(s_mic1_envelope));
    memset(&s_mic2_envelope, 0, sizeof(s_mic2_envelope));
    s_initialized = true;

    ESP_LOGI(TAG, "FFT ready: %u points, %u log bands, %.0f-%.0f Hz",
             AUDIO_SPATIAL_FFT_SIZE, AUDIO_SPECTRUM_BAND_COUNT,
             SPECTRUM_MIN_FREQUENCY_HZ, SPECTRUM_MAX_FREQUENCY_HZ);
    return ESP_OK;
}

esp_err_t audio_spatial_spectrum_processor_process(
    const int16_t *mic1_samples,
    const int16_t *mic2_samples,
    audio_spatial_spectrum_result_t *result)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }
    if (NULL == mic1_samples || NULL == mic2_samples || NULL == result) {
        return ESP_ERR_INVALID_ARG;
    }

    const float mic1_mean = calculate_mean(mic1_samples);
    const float mic2_mean = calculate_mean(mic2_samples);
    for (size_t index = 0; index < AUDIO_SPATIAL_FFT_SIZE; index++) {
        s_fft_buffer[index * 2U] =
            ((float)mic1_samples[index] - mic1_mean) *
            s_hamming_window[index] / PCM_FULL_SCALE;
        s_fft_buffer[index * 2U + 1U] =
            ((float)mic2_samples[index] - mic2_mean) *
            s_hamming_window[index] / PCM_FULL_SCALE;
    }

    esp_err_t ret = dsps_fft2r_fc32(s_fft_buffer, AUDIO_SPATIAL_FFT_SIZE);
    if (ESP_OK != ret) {
        return ret;
    }
    ret = dsps_bit_rev_fc32(s_fft_buffer, AUDIO_SPATIAL_FFT_SIZE);
    if (ESP_OK != ret) {
        return ret;
    }
    ret = dsps_cplx2reC_fc32(s_fft_buffer, AUDIO_SPATIAL_FFT_SIZE);
    if (ESP_OK != ret) {
        return ret;
    }

    float mic1_targets[AUDIO_SPECTRUM_BAND_COUNT] = {0};
    float mic2_targets[AUDIO_SPECTRUM_BAND_COUNT] = {0};
    float combined_targets[AUDIO_SPECTRUM_BAND_COUNT] = {0};
    const float *mic1_spectrum = &s_fft_buffer[0];
    const float *mic2_spectrum = &s_fft_buffer[AUDIO_SPATIAL_FFT_SIZE];

    for (size_t index = 0; index < AUDIO_SPECTRUM_BAND_COUNT; index++) {
        const float mic1_magnitude = calculate_band_magnitude(mic1_spectrum,
                                                               &s_bands[index]);
        const float mic2_magnitude = calculate_band_magnitude(mic2_spectrum,
                                                               &s_bands[index]);
        const float combined_magnitude =
            audio_spectrum_combine_magnitude(mic1_magnitude, mic2_magnitude);
        mic1_targets[index] = magnitude_to_level(mic1_magnitude);
        mic2_targets[index] = magnitude_to_level(mic2_magnitude);
        combined_targets[index] = magnitude_to_level(combined_magnitude);
    }

    audio_spectrum_envelope_update(&s_mic1_envelope, mic1_targets,
                                   AUDIO_SPECTRUM_BAND_COUNT,
                                   SPECTRUM_ATTACK, SPECTRUM_RELEASE,
                                   SPECTRUM_PEAK_DECAY);
    audio_spectrum_envelope_update(&s_mic2_envelope, mic2_targets,
                                   AUDIO_SPECTRUM_BAND_COUNT,
                                   SPECTRUM_ATTACK, SPECTRUM_RELEASE,
                                   SPECTRUM_PEAK_DECAY);
    audio_spectrum_envelope_update(&s_combined_envelope, combined_targets,
                                   AUDIO_SPECTRUM_BAND_COUNT,
                                   SPECTRUM_ATTACK, SPECTRUM_RELEASE,
                                   SPECTRUM_PEAK_DECAY);

    memcpy(result->mic1_levels, s_mic1_envelope.level, sizeof(result->mic1_levels));
    memcpy(result->mic1_peaks, s_mic1_envelope.peak, sizeof(result->mic1_peaks));
    memcpy(result->mic2_levels, s_mic2_envelope.level, sizeof(result->mic2_levels));
    memcpy(result->mic2_peaks, s_mic2_envelope.peak, sizeof(result->mic2_peaks));
    memcpy(result->combined_levels, s_combined_envelope.level,
           sizeof(result->combined_levels));
    memcpy(result->combined_peaks, s_combined_envelope.peak,
           sizeof(result->combined_peaks));

    result->mic1_rms = calculate_rms(mic1_samples);
    result->mic2_rms = calculate_rms(mic2_samples);
    result->mic1_level = calculate_rms_level(result->mic1_rms);
    result->mic2_level = calculate_rms_level(result->mic2_rms);
    result->energy_db = calculate_energy_db(result->mic1_rms, result->mic2_rms);
    result->energy_dbfs = audio_spectrum_dbfs_from_rms_pair(result->mic1_rms,
                                                             result->mic2_rms,
                                                             (uint32_t)PCM_FULL_SCALE);
    return ESP_OK;
}
