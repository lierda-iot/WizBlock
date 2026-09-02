#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    AUDIO_PROCESSOR_AFE_MODE_DEFAULT = 0,
    AUDIO_PROCESSOR_AFE_MODE_HIGH_PERF,
    AUDIO_PROCESSOR_AFE_MODE_LOW_COST,
} audio_processor_afe_mode_t;

typedef struct {
    uint8_t mic_channels;
    uint8_t ref_channels;
    bool enable_ns;
    bool enable_aec;
    bool enable_vad;
    bool enable_wakenet;
    float wakenet_threshold;
    int aec_mode;
    audio_processor_afe_mode_t afe_mode;
    bool afe_task_policy_valid;
    uint8_t afe_task_core;
    uint8_t afe_task_priority;
} audio_processor_config_t;

audio_processor_afe_mode_t audio_processor_resolve_afe_mode(
    const audio_processor_config_t *config);
esp_err_t audio_processor_init(const audio_processor_config_t *cfg);
size_t audio_processor_get_feed_chunksize(void);
size_t audio_processor_get_fetch_chunksize(void);
esp_err_t audio_processor_feed(const int16_t *data, size_t samples_per_channel);
esp_err_t audio_processor_fetch(int16_t *out, size_t *out_samples, bool *vad_active, bool *wakeup);
esp_err_t audio_processor_disable_aec(void);
esp_err_t audio_processor_enable_aec(void);
esp_err_t audio_processor_reset_buffer(void);
void audio_processor_deinit(void);
