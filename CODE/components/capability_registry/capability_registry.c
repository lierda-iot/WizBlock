#include "capability_registry.h"

#include <stdbool.h>

static bool s_initialized;
static capability_state_t s_states[CAPABILITY_MAX];

esp_err_t capability_registry_init(void)
{
    for (int i = 0; i < CAPABILITY_MAX; ++i) {
        s_states[i] = CAPABILITY_STATE_UNAVAILABLE;
    }
    s_initialized = true;
    return ESP_OK;
}

esp_err_t capability_registry_set_state(capability_id_t id, capability_state_t state)
{
    if (!s_initialized || id >= CAPABILITY_MAX) {
        return ESP_ERR_INVALID_STATE;
    }
    s_states[id] = state;
    return ESP_OK;
}

capability_state_t capability_registry_get_state(capability_id_t id)
{
    if (!s_initialized || id >= CAPABILITY_MAX) {
        return CAPABILITY_STATE_FAULT;
    }
    return s_states[id];
}

const char *capability_registry_id_name(capability_id_t id)
{
    static const char *const names[CAPABILITY_MAX] = {
        [CAPABILITY_DISPLAY] = "display",
        [CAPABILITY_TOUCH] = "touch",
        [CAPABILITY_CAMERA] = "camera",
        [CAPABILITY_TRACK_MOTION] = "track_motion",
        [CAPABILITY_AUDIO_IO] = "audio_io",
        [CAPABILITY_LTE] = "lte",
        [CAPABILITY_STORAGE] = "storage",
    };

    if (id >= CAPABILITY_MAX) {
        return "unknown";
    }
    return names[id];
}

const char *capability_registry_state_name(capability_state_t state)
{
    switch (state) {
    case CAPABILITY_STATE_UNAVAILABLE:
        return "unavailable";
    case CAPABILITY_STATE_AVAILABLE:
        return "available";
    case CAPABILITY_STATE_FAULT:
        return "fault";
    case CAPABILITY_STATE_PENDING_DRIVER:
        return "pending_driver";
    default:
        return "unknown";
    }
}
