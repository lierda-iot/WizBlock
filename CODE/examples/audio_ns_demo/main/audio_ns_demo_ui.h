#pragma once

#include "audio_ns_demo_logic.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define AUDIO_NS_DEMO_ERROR_TEXT_LEN 64U

typedef struct {
    audio_ns_demo_state_t state;
    size_t completed_ms;
    size_t total_ms;
    uint8_t progress_percent;
    uint32_t raw_rms;
    uint32_t denoised_rms;
    bool rms_ready;
    char error_text[AUDIO_NS_DEMO_ERROR_TEXT_LEN];
} audio_ns_demo_status_t;

typedef void (*audio_ns_demo_restart_cb_t)(void *user_ctx);

typedef struct {
    audio_ns_demo_restart_cb_t on_restart;
    void *user_ctx;
} audio_ns_demo_ui_callbacks_t;

esp_err_t audio_ns_demo_ui_init(const audio_ns_demo_ui_callbacks_t *callbacks);
esp_err_t audio_ns_demo_ui_update(const audio_ns_demo_status_t *status);
