#pragma once

#include <stdbool.h>

typedef enum {
    HOLO_DISPLAY_MAIN = 0,
    HOLO_DISPLAY_WIFI_CONFIG,
    HOLO_DISPLAY_WIFI_KEYBOARD,
    HOLO_DISPLAY_HOME_HANDOFF,
} holocubic_display_mode_t;

typedef struct {
    holocubic_display_mode_t mode;
    bool has_wifi_credentials;
    bool connect_pending;
} holocubic_ui_state_t;

void holocubic_ui_state_init(holocubic_ui_state_t *state,
                             bool has_wifi_credentials);
void holocubic_ui_open_wifi(holocubic_ui_state_t *state);
void holocubic_ui_open_keyboard(holocubic_ui_state_t *state);
void holocubic_ui_close_keyboard(holocubic_ui_state_t *state);
void holocubic_ui_connect_submitted(holocubic_ui_state_t *state);
void holocubic_ui_wifi_ready(holocubic_ui_state_t *state);
void holocubic_ui_home_handoff_complete(holocubic_ui_state_t *state);
bool holocubic_ui_main_input_enabled(const holocubic_ui_state_t *state);
bool holocubic_ui_wifi_input_enabled(const holocubic_ui_state_t *state);
