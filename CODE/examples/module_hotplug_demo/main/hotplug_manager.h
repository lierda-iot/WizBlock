#pragma once

#include "esp_err.h"
#include <stdbool.h>
#include <stdint.h>

typedef enum {
    HOTPLUG_STATE_ABSENT = 0,
    HOTPLUG_STATE_PRESENT,
    HOTPLUG_STATE_UNKNOWN,
} hotplug_state_t;

typedef struct {
    const char *name;
    hotplug_state_t (*detect_fn)(void);
} hotplug_slot_t;

typedef void (*hotplug_event_cb_t)(const char *slot_name, hotplug_state_t new_state, void *user_ctx);

esp_err_t hotplug_manager_register_slot(const hotplug_slot_t *slot);
esp_err_t hotplug_manager_set_event_cb(hotplug_event_cb_t cb, void *user_ctx);
esp_err_t hotplug_manager_start(uint32_t poll_interval_ms);
esp_err_t hotplug_manager_stop(void);
hotplug_state_t hotplug_manager_get_state(const char *name);
