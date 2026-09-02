#pragma once

#include "esp_err.h"

typedef enum {
    CAPABILITY_DISPLAY = 0,
    CAPABILITY_TOUCH,
    CAPABILITY_CAMERA,
    CAPABILITY_TRACK_MOTION,
    CAPABILITY_AUDIO_IO,
    CAPABILITY_LTE,
    CAPABILITY_STORAGE,
    CAPABILITY_MAX,
} capability_id_t;

typedef enum {
    CAPABILITY_STATE_UNAVAILABLE = 0,
    CAPABILITY_STATE_AVAILABLE,
    CAPABILITY_STATE_FAULT,
    CAPABILITY_STATE_PENDING_DRIVER,
} capability_state_t;

esp_err_t capability_registry_init(void);
esp_err_t capability_registry_set_state(capability_id_t id, capability_state_t state);
capability_state_t capability_registry_get_state(capability_id_t id);
const char *capability_registry_id_name(capability_id_t id);
const char *capability_registry_state_name(capability_state_t state);
