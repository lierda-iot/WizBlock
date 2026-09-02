#pragma once

#include "audio_opus_demo_logic.h"

#include "esp_err.h"

#include <stddef.h>
#include <stdint.h>

#define AUDIO_OPUS_DEMO_ERROR_TEXT_LEN 64U

typedef struct {
    audio_opus_demo_state_t state;
    size_t completed_units;
    size_t total_units;
    uint8_t progress_percent;
    uint32_t packet_count;
    uint32_t raw_bytes;
    uint32_t encoded_bytes;
    uint32_t compression_percent;
    char error_text[AUDIO_OPUS_DEMO_ERROR_TEXT_LEN];
} audio_opus_demo_status_t;

typedef void (*audio_opus_demo_restart_cb_t)(void *user_ctx);

typedef struct {
    audio_opus_demo_restart_cb_t on_restart;
    void *user_ctx;
} audio_opus_demo_ui_callbacks_t;

esp_err_t audio_opus_demo_ui_init(const audio_opus_demo_ui_callbacks_t *callbacks);
esp_err_t audio_opus_demo_ui_update(const audio_opus_demo_status_t *status);
