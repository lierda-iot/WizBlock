#include "holocubic_ui_state.h"

#include <stddef.h>

void holocubic_ui_state_init(holocubic_ui_state_t *state,
                             bool has_wifi_credentials)
{
    if (NULL == state) {
        return;
    }
    *state = (holocubic_ui_state_t){
        .mode = has_wifi_credentials ? HOLO_DISPLAY_MAIN :
                                       HOLO_DISPLAY_WIFI_CONFIG,
        .has_wifi_credentials = has_wifi_credentials,
        .connect_pending = !has_wifi_credentials,
    };
}

void holocubic_ui_open_wifi(holocubic_ui_state_t *state)
{
    if (NULL != state) {
        state->mode = HOLO_DISPLAY_WIFI_CONFIG;
        state->connect_pending = false;
    }
}

void holocubic_ui_open_keyboard(holocubic_ui_state_t *state)
{
    if (NULL != state && HOLO_DISPLAY_WIFI_CONFIG == state->mode) {
        state->mode = HOLO_DISPLAY_WIFI_KEYBOARD;
    }
}

void holocubic_ui_close_keyboard(holocubic_ui_state_t *state)
{
    if (NULL != state && HOLO_DISPLAY_WIFI_KEYBOARD == state->mode) {
        state->mode = HOLO_DISPLAY_WIFI_CONFIG;
    }
}

void holocubic_ui_connect_submitted(holocubic_ui_state_t *state)
{
    if (NULL != state && HOLO_DISPLAY_WIFI_CONFIG == state->mode) {
        state->has_wifi_credentials = true;
        state->connect_pending = true;
    }
}

void holocubic_ui_wifi_ready(holocubic_ui_state_t *state)
{
    if (NULL != state && state->connect_pending) {
        state->has_wifi_credentials = true;
        state->mode = HOLO_DISPLAY_HOME_HANDOFF;
        state->connect_pending = false;
    }
}

void holocubic_ui_home_handoff_complete(holocubic_ui_state_t *state)
{
    if (NULL != state && HOLO_DISPLAY_HOME_HANDOFF == state->mode) {
        state->mode = HOLO_DISPLAY_MAIN;
    }
}

bool holocubic_ui_main_input_enabled(const holocubic_ui_state_t *state)
{
    return NULL != state && HOLO_DISPLAY_MAIN == state->mode;
}

bool holocubic_ui_wifi_input_enabled(const holocubic_ui_state_t *state)
{
    return NULL != state &&
           (HOLO_DISPLAY_WIFI_CONFIG == state->mode ||
            HOLO_DISPLAY_WIFI_KEYBOARD == state->mode);
}
