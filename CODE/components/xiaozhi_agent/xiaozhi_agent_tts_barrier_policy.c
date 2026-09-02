#include "xiaozhi_agent_tts_barrier_policy.h"

#include <stddef.h>
#include <string.h>

static bool barrier_is_bound(const xiaozhi_agent_tts_barrier_t *barrier)
{
    return NULL != barrier && 0U != barrier->session_epoch &&
           0U != barrier->request_id;
}

void xiaozhi_agent_tts_barrier_reset(
    xiaozhi_agent_tts_barrier_t *barrier,
    uint32_t session_epoch, uint32_t request_id)
{
    if (NULL == barrier) {
        return;
    }
    *barrier = (xiaozhi_agent_tts_barrier_t){0};
    if (0U != session_epoch && 0U != request_id) {
        barrier->session_epoch = session_epoch;
        barrier->request_id = request_id;
    }
}

void xiaozhi_agent_tts_barrier_on_listening_ready(
    xiaozhi_agent_tts_barrier_t *barrier)
{
    if (barrier_is_bound(barrier)) {
        barrier->listening_ready = true;
    }
}

void xiaozhi_agent_tts_barrier_on_listen_stop(
    xiaozhi_agent_tts_barrier_t *barrier)
{
    if (barrier_is_bound(barrier) && barrier->listening_ready) {
        barrier->listen_stop_sent = true;
    }
}

void xiaozhi_agent_tts_barrier_on_processing_evidence(
    xiaozhi_agent_tts_barrier_t *barrier)
{
    if (barrier_is_bound(barrier) && barrier->listening_ready &&
        barrier->listen_stop_sent) {
        barrier->processing_evidence = true;
    }
}

bool xiaozhi_agent_tts_barrier_type_is_processing_evidence(
    const char *type)
{
    return NULL != type &&
           (0 == strcmp(type, "stt") || 0 == strcmp(type, "llm"));
}

bool xiaozhi_agent_tts_state_starts_speaking(const char *state)
{
    return NULL != state &&
           (0 == strcmp(state, "start") ||
            0 == strcmp(state, "sentence_start"));
}

bool xiaozhi_agent_tts_barrier_accepts(
    const xiaozhi_agent_tts_barrier_t *barrier,
    uint32_t session_epoch, uint32_t request_id)
{
    return barrier_is_bound(barrier) && barrier->listening_ready &&
           barrier->listen_stop_sent && barrier->processing_evidence &&
           session_epoch == barrier->session_epoch &&
           request_id == barrier->request_id;
}
