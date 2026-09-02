#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t session_epoch;
    uint32_t request_id;
    bool listening_ready;
    bool listen_stop_sent;
    bool processing_evidence;
} xiaozhi_agent_tts_barrier_t;

void xiaozhi_agent_tts_barrier_reset(
    xiaozhi_agent_tts_barrier_t *barrier,
    uint32_t session_epoch, uint32_t request_id);
void xiaozhi_agent_tts_barrier_on_listening_ready(
    xiaozhi_agent_tts_barrier_t *barrier);
void xiaozhi_agent_tts_barrier_on_listen_stop(
    xiaozhi_agent_tts_barrier_t *barrier);
void xiaozhi_agent_tts_barrier_on_processing_evidence(
    xiaozhi_agent_tts_barrier_t *barrier);
bool xiaozhi_agent_tts_barrier_type_is_processing_evidence(
    const char *type);
bool xiaozhi_agent_tts_state_starts_speaking(const char *state);
bool xiaozhi_agent_tts_barrier_accepts(
    const xiaozhi_agent_tts_barrier_t *barrier,
    uint32_t session_epoch, uint32_t request_id);
