#include "companion_audio_voice_gate.h"

#include <limits.h>
#include <math.h>
#include <stddef.h>

#define COMPANION_AUDIO_VOICE_MIN_NOISE_FRAMES 4U
#define COMPANION_AUDIO_VOICE_MIN_SNR_MILLI 1800U
#define COMPANION_AUDIO_VOICE_MAX_CREST_MILLI 12000U
#define COMPANION_AUDIO_VOICE_MIN_ZCR_PERMILLE 15U
#define COMPANION_AUDIO_VOICE_MAX_ZCR_PERMILLE 350U

static uint32_t ratio_milli(uint32_t signal, uint32_t baseline)
{
    if (0U == baseline) {
        return 0U;
    }
    const uint64_t ratio = (uint64_t)signal * 1000ULL / baseline;
    return (UINT32_MAX < ratio) ? UINT32_MAX : (uint32_t)ratio;
}

bool companion_audio_voice_features_from_pcm(
    const int16_t *samples, size_t count,
    companion_audio_voice_features_t *features)
{
    if (NULL == samples || 0U == count || NULL == features) {
        return false;
    }
    uint64_t square_sum = 0ULL;
    uint32_t peak = 0U;
    uint32_t zero_crossings = 0U;
    int previous_sign = 0;
    for (size_t index = 0U; index < count; ++index) {
        const int32_t sample = samples[index];
        const uint32_t magnitude =
            (0 > sample) ? (uint32_t)(-sample) : (uint32_t)sample;
        const uint64_t square = (uint64_t)((int64_t)sample * sample);
        if (UINT64_MAX - square_sum < square) {
            return false;
        }
        square_sum += square;
        if (magnitude > peak) {
            peak = magnitude;
        }
        const int sign = (0 < sample) ? 1 : ((0 > sample) ? -1 : 0);
        if (0 != sign) {
            if (0 != previous_sign && sign != previous_sign) {
                zero_crossings++;
            }
            previous_sign = sign;
        }
    }
    const uint64_t mean_square = square_sum / count;
    const float rms = sqrtf((float)mean_square);
    if (rms > (float)UINT32_MAX) {
        return false;
    }
    *features = (companion_audio_voice_features_t){
        .rms = (uint32_t)rms,
        .peak = peak,
        .zero_crossing_permille = (1U < count) ?
            (uint32_t)((uint64_t)zero_crossings * 1000ULL /
                       (count - 1U)) : 0U,
    };
    return true;
}

void companion_audio_voice_gate_config_default(
    companion_audio_voice_gate_config_t *config)
{
    if (NULL != config) {
        *config = (companion_audio_voice_gate_config_t){
            .minimum_noise_frames = COMPANION_AUDIO_VOICE_MIN_NOISE_FRAMES,
            .minimum_snr_milli = COMPANION_AUDIO_VOICE_MIN_SNR_MILLI,
            .maximum_crest_milli = COMPANION_AUDIO_VOICE_MAX_CREST_MILLI,
            .minimum_zero_crossing_permille =
                COMPANION_AUDIO_VOICE_MIN_ZCR_PERMILLE,
            .maximum_zero_crossing_permille =
                COMPANION_AUDIO_VOICE_MAX_ZCR_PERMILLE,
        };
    }
}

void companion_audio_voice_gate_init(
    companion_audio_voice_gate_t *gate,
    const companion_audio_voice_gate_config_t *config)
{
    if (NULL == gate || NULL == config ||
        0U == config->minimum_noise_frames ||
        1000U > config->minimum_snr_milli ||
        1000U > config->maximum_crest_milli ||
        config->minimum_zero_crossing_permille >=
            config->maximum_zero_crossing_permille ||
        1000U < config->maximum_zero_crossing_permille) {
        return;
    }
    *gate = (companion_audio_voice_gate_t){.config = *config};
}

companion_audio_voice_gate_result_t companion_audio_voice_gate_step(
    companion_audio_voice_gate_t *gate, bool vad_active,
    const companion_audio_voice_features_t *features, uint64_t now_ms)
{
    (void)now_ms;
    companion_audio_voice_gate_result_t result = {0};
    if (NULL == gate || NULL == features ||
        0U == gate->config.minimum_noise_frames) {
        return result;
    }
    if (!vad_active) {
        if (0U == gate->noise_frames) {
            gate->noise_rms = features->rms;
        } else {
            gate->noise_rms =
                (gate->noise_rms * 7U + features->rms) / 8U;
        }
        if (UINT32_MAX != gate->noise_frames) {
            gate->noise_frames++;
        }
    }
    result.noise_rms = gate->noise_rms;
    result.baseline_ready =
        gate->noise_frames >= gate->config.minimum_noise_frames;
    result.snr_milli = ratio_milli(features->rms, gate->noise_rms);
    result.crest_milli = ratio_milli(features->peak, features->rms);
    result.zero_crossing_permille = features->zero_crossing_permille;
    result.evidence_active = vad_active && result.baseline_ready &&
        result.snr_milli >= gate->config.minimum_snr_milli &&
        result.crest_milli <= gate->config.maximum_crest_milli &&
        result.zero_crossing_permille >=
            gate->config.minimum_zero_crossing_permille &&
        result.zero_crossing_permille <=
            gate->config.maximum_zero_crossing_permille;
    return result;
}
