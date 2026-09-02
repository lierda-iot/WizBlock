#pragma once

#include <stdbool.h>
#include <stdint.h>

#define XIAOZHI_AGENT_VAD_MIN_TX_FRAMES 20U

typedef struct {
    uint32_t request_id;
    uint32_t tx_success_frames;
    bool listening;
    bool vad_stop_pending;
    bool vad_stop_sent;
} xiaozhi_agent_vad_stop_policy_t;

void xiaozhi_agent_vad_stop_policy_reset(
    xiaozhi_agent_vad_stop_policy_t *policy, uint32_t request_id);
bool xiaozhi_agent_vad_stop_policy_set_listening(
    xiaozhi_agent_vad_stop_policy_t *policy, uint32_t request_id,
    bool listening);
bool xiaozhi_agent_vad_stop_policy_accept_vad_end(
    xiaozhi_agent_vad_stop_policy_t *policy, uint32_t request_id);
bool xiaozhi_agent_vad_stop_policy_accept_vad_start(
    xiaozhi_agent_vad_stop_policy_t *policy, uint32_t request_id);
bool xiaozhi_agent_vad_stop_policy_note_tx_success(
    xiaozhi_agent_vad_stop_policy_t *policy, uint32_t request_id);
bool xiaozhi_agent_vad_stop_policy_ready(
    const xiaozhi_agent_vad_stop_policy_t *policy, uint32_t request_id);
bool xiaozhi_agent_vad_stop_policy_mark_stop_sent(
    xiaozhi_agent_vad_stop_policy_t *policy, uint32_t request_id);
