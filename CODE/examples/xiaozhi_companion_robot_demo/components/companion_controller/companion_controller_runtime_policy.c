#include "companion_controller_runtime_policy.h"

#include <stddef.h>

#define COMPANION_CONTROLLER_TTS_ACTIVITY_INTERVAL_MS 1000ULL

esp_err_t companion_controller_tts_activity_record(
    companion_controller_tts_activity_fact_t *fact,
    uint32_t generation, uint32_t wake_seq, uint32_t session_epoch,
    uint32_t request_id, uint64_t timestamp_ms)
{
    if (NULL == fact || 0U == generation || 0U == wake_seq ||
        0U == session_epoch || 0U == request_id) {
        return ESP_ERR_INVALID_ARG;
    }
    const bool same_token = fact->valid &&
        generation == fact->generation && wake_seq == fact->wake_seq &&
        session_epoch == fact->session_epoch && request_id == fact->request_id;
    if (same_token && timestamp_ms < fact->timestamp_ms) {
        return ESP_ERR_INVALID_STATE;
    }
    if (same_token && COMPANION_CONTROLLER_TTS_ACTIVITY_INTERVAL_MS >
        timestamp_ms - fact->timestamp_ms) {
        return ESP_OK;
    }
    fact->valid = true;
    fact->generation = generation;
    fact->wake_seq = wake_seq;
    fact->session_epoch = session_epoch;
    fact->request_id = request_id;
    fact->timestamp_ms = timestamp_ms;
    fact->revision++;
    if (0U == fact->revision) {
        fact->revision = 1U;
    }
    return ESP_OK;
}

bool companion_controller_roam_runtime_is_valid(
    companion_product_state_t product_state,
    bool roam_requested,
    bool wake_reserved,
    bool roam_running)
{
    if (!roam_running) {
        return true;
    }
    const bool normal_roam =
        COMPANION_PRODUCT_IDLE == product_state && roam_requested &&
        !wake_reserved;
    const bool reserved_wake_transition =
        COMPANION_PRODUCT_IDLE == product_state && roam_requested &&
        wake_reserved;
    return normal_roam || reserved_wake_transition;
}
