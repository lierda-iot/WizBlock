#pragma once

#include "companion_logic.h"
#include "companion_merit_tap.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef enum {
    COMPANION_MOTION_ROLE_ROAM = 0,
    COMPANION_MOTION_ROLE_WAKE_TURN,
} companion_motion_role_t;

typedef struct {
    companion_action_t action;
    companion_motion_role_t role;
    uint32_t duration_ms;
    float target_deg;
    uint32_t generation;
    uint32_t wake_seq;
    uint32_t request_id;
} companion_motion_command_t;

typedef enum {
    COMPANION_MOTION_RESULT_SUCCESS = 0,
    COMPANION_MOTION_RESULT_CANCELLED,
    COMPANION_MOTION_RESULT_RETRYABLE_SENSOR_FAILURE,
    COMPANION_MOTION_RESULT_PERMANENT_OUTPUT_FAILURE,
} companion_motion_result_class_t;

typedef struct {
    esp_err_t error;
    esp_err_t stop_error;
    companion_motion_result_class_t classification;
    bool available;
} companion_motion_result_t;

typedef struct {
    float target_deg;
    float turned_deg;
    float remaining_deg;
} companion_motion_progress_t;

typedef void (*companion_motion_done_cb_t)(const companion_motion_command_t *command,
                                           const companion_motion_result_t *result,
                                           void *user_ctx);
typedef void (*companion_motion_progress_cb_t)(
    const companion_motion_command_t *command,
    const companion_motion_progress_t *progress,
    void *user_ctx);
typedef void (*companion_motion_merit_tap_cb_t)(
    const companion_merit_result_t *result, uint32_t generation,
    uint32_t wake_seq, uint64_t timestamp_us, void *user_ctx);

typedef struct {
    companion_motion_done_cb_t on_done;
    companion_motion_progress_cb_t on_progress;
    companion_motion_merit_tap_cb_t on_merit_tap;
    companion_merit_tap_config_t merit_tap_config;
    void *user_ctx;
} companion_motion_config_t;

esp_err_t companion_motion_start(const companion_motion_config_t *config);
esp_err_t companion_motion_submit(const companion_motion_command_t *command);
esp_err_t companion_motion_stop(const char *reason);
esp_err_t companion_motion_stop_role(companion_motion_role_t role,
                                     const char *reason);
esp_err_t companion_motion_set_merit_tap_gate(bool enabled,
                                              uint32_t generation,
                                              uint32_t wake_seq);
bool companion_motion_is_available(void);
