#pragma once

#include <stdbool.h>

#include "esp_audio_render.h"
#include "esp_err.h"

esp_err_t mp3_audio_output_init(
    esp_audio_render_stream_handle_t *render_stream);

esp_err_t mp3_audio_output_set_amp(bool enabled);

void mp3_audio_output_deinit(void);
