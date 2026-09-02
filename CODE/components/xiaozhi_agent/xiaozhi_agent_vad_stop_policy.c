#include "xiaozhi_agent_vad_stop_policy.h"

#include <stddef.h>

static bool policy_matches(const xiaozhi_agent_vad_stop_policy_t *policy,
                           uint32_t request_id)
{
    return NULL != policy && 0U != request_id &&
           request_id == policy->request_id;
}

void xiaozhi_agent_vad_stop_policy_reset(
    xiaozhi_agent_vad_stop_policy_t *policy, uint32_t request_id)
{
    if (NULL == policy) {
        return;
    }
    *policy = (xiaozhi_agent_vad_stop_policy_t){
        .request_id = request_id,
    };
}

bool xiaozhi_agent_vad_stop_policy_set_listening(
    xiaozhi_agent_vad_stop_policy_t *policy, uint32_t request_id,
    bool listening)
{
    if (!policy_matches(policy, request_id) || policy->vad_stop_sent) {
        return false;
    }
    policy->listening = listening;
    return true;
}

bool xiaozhi_agent_vad_stop_policy_accept_vad_end(
    xiaozhi_agent_vad_stop_policy_t *policy, uint32_t request_id)
{
    if (!policy_matches(policy, request_id) || !policy->listening ||
        policy->vad_stop_sent) {
        return false;
    }
    policy->vad_stop_pending = true;
    return true;
}

bool xiaozhi_agent_vad_stop_policy_accept_vad_start(
    xiaozhi_agent_vad_stop_policy_t *policy, uint32_t request_id)
{
    if (!policy_matches(policy, request_id) || !policy->listening ||
        policy->vad_stop_sent) {
        return false;
    }
    policy->vad_stop_pending = false;
    return true;
}

bool xiaozhi_agent_vad_stop_policy_note_tx_success(
    xiaozhi_agent_vad_stop_policy_t *policy, uint32_t request_id)
{
    if (!policy_matches(policy, request_id) || !policy->listening ||
        policy->vad_stop_sent ||
        policy->tx_success_frames == UINT32_MAX) {
        return false;
    }
    policy->tx_success_frames++;
    return policy->vad_stop_pending &&
           XIAOZHI_AGENT_VAD_MIN_TX_FRAMES ==
               policy->tx_success_frames;
}

bool xiaozhi_agent_vad_stop_policy_ready(
    const xiaozhi_agent_vad_stop_policy_t *policy, uint32_t request_id)
{
    return policy_matches(policy, request_id) && policy->listening &&
           policy->vad_stop_pending && !policy->vad_stop_sent &&
           policy->tx_success_frames >= XIAOZHI_AGENT_VAD_MIN_TX_FRAMES;
}

bool xiaozhi_agent_vad_stop_policy_mark_stop_sent(
    xiaozhi_agent_vad_stop_policy_t *policy, uint32_t request_id)
{
    if (!xiaozhi_agent_vad_stop_policy_ready(policy, request_id)) {
        return false;
    }
    policy->vad_stop_pending = false;
    policy->vad_stop_sent = true;
    policy->listening = false;
    return true;
}
