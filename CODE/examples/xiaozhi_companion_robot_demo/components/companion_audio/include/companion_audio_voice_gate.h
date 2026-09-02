#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct {
    uint32_t minimum_noise_frames;
    uint32_t minimum_snr_milli;
    uint32_t maximum_crest_milli;
    uint32_t minimum_zero_crossing_permille;
    uint32_t maximum_zero_crossing_permille;
} companion_audio_voice_gate_config_t;

typedef struct {
    uint32_t rms;
    uint32_t peak;
    uint32_t zero_crossing_permille;
} companion_audio_voice_features_t;

typedef struct {
    companion_audio_voice_gate_config_t config;
    uint32_t noise_rms;
    uint32_t noise_frames;
} companion_audio_voice_gate_t;

typedef struct {
    bool baseline_ready;
    bool evidence_active;
    uint32_t noise_rms;
    uint32_t snr_milli;
    uint32_t crest_milli;
    uint32_t zero_crossing_permille;
} companion_audio_voice_gate_result_t;

bool companion_audio_voice_features_from_pcm(
    const int16_t *samples, size_t count,
    companion_audio_voice_features_t *features);
void companion_audio_voice_gate_config_default(
    companion_audio_voice_gate_config_t *config);
void companion_audio_voice_gate_init(
    companion_audio_voice_gate_t *gate,
    const companion_audio_voice_gate_config_t *config);
companion_audio_voice_gate_result_t companion_audio_voice_gate_step(
    companion_audio_voice_gate_t *gate, bool vad_active,
    const companion_audio_voice_features_t *features, uint64_t now_ms);
