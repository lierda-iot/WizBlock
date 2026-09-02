#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

typedef enum {
    DOA_UI_DIRECTION_IDLE = 0,
    DOA_UI_DIRECTION_LEFT,
    DOA_UI_DIRECTION_CENTER,
    DOA_UI_DIRECTION_RIGHT,
} doa_ui_direction_t;

typedef struct {
    bool active;
    doa_ui_direction_t direction;
    float angle_deg;
    float relative_deg;
    float energy_db;
    uint32_t mic1_rms;
    uint32_t mic2_rms;
} doa_ui_state_t;

esp_err_t audio_dual_mic_doa_ui_init(void);
esp_err_t audio_dual_mic_doa_ui_update(const doa_ui_state_t *state);
