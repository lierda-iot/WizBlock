#pragma once

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef void (*companion_input_click_cb_t)(void *user_ctx);
typedef void (*companion_input_long_press_cb_t)(void *user_ctx);
typedef void (*companion_input_error_cb_t)(esp_err_t error, void *user_ctx);
typedef void (*companion_input_health_cb_t)(bool available, esp_err_t error,
                                            void *user_ctx);

typedef struct {
    int pressed_raw_max;
    int released_raw_min;
    uint32_t sample_ms;
    uint32_t debounce_ms;
    uint32_t max_click_ms;
    uint32_t long_press_ms;
    companion_input_click_cb_t on_click;
    companion_input_long_press_cb_t on_long_press;
    companion_input_error_cb_t on_error;
    companion_input_health_cb_t on_health;
    void *user_ctx;
} companion_input_config_t;

esp_err_t companion_input_start(const companion_input_config_t *config);
