#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef void (*xiaozhi_audio_on_recv_cb_t)(const uint8_t *opus_data, int len, void *user_ctx);
typedef void (*xiaozhi_audio_on_event_cb_t)(int event, void *user_ctx);

#define XIAOZHI_AUDIO_EVENT_WAKE_WORD   1
#define XIAOZHI_AUDIO_EVENT_VAD_START   2
#define XIAOZHI_AUDIO_EVENT_VAD_END     3

typedef struct {
    xiaozhi_audio_on_recv_cb_t on_opus_recv;
    xiaozhi_audio_on_event_cb_t on_event;
    void *user_ctx;
} xiaozhi_audio_config_t;

esp_err_t xiaozhi_audio_init(const xiaozhi_audio_config_t *config);
esp_err_t xiaozhi_audio_start(void);
esp_err_t xiaozhi_audio_stop(void);
esp_err_t xiaozhi_audio_play_opus(const uint8_t *data, int len);
esp_err_t xiaozhi_audio_play_stop(void);
esp_err_t xiaozhi_audio_play_tone(uint32_t freq_hz, uint32_t duration_ms, float volume);
esp_err_t xiaozhi_audio_prompt_play(const char *url);
esp_err_t xiaozhi_audio_prompt_stop(void);
