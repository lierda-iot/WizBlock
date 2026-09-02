#pragma once

#include "companion_logic.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    bool valid;
    uint32_t generation;
    uint32_t wake_seq;
    uint32_t session_epoch;
    uint32_t request_id;
    uint32_t revision;
    uint64_t timestamp_ms;
} companion_controller_tts_activity_fact_t;

esp_err_t companion_controller_tts_activity_record(
    companion_controller_tts_activity_fact_t *fact,
    uint32_t generation, uint32_t wake_seq, uint32_t session_epoch,
    uint32_t request_id, uint64_t timestamp_ms);

bool companion_controller_roam_runtime_is_valid(
    companion_product_state_t product_state,
    bool roam_requested,
    bool wake_reserved,
    bool roam_running);
