#include "companion_agent_binding_policy.h"

#include <stddef.h>

static bool binding_is_valid(const companion_agent_binding_id_t *binding)
{
    return NULL != binding && 0U != binding->generation &&
           0U != binding->wake_seq && 0U != binding->request_id;
}

companion_agent_binding_route_t companion_agent_binding_route_audio(
    const companion_agent_binding_id_t *current,
    uint32_t event_request_id)
{
    if (0U == event_request_id) {
        return COMPANION_AGENT_BINDING_DROP;
    }
    if (binding_is_valid(current) &&
        event_request_id == current->request_id) {
        return COMPANION_AGENT_BINDING_CURRENT;
    }
    return COMPANION_AGENT_BINDING_DROP;
}

bool companion_agent_binding_retire_if_current(
    companion_agent_binding_id_t *current,
    const companion_agent_binding_id_t *expected)
{
    if (!binding_is_valid(current) || !binding_is_valid(expected) ||
        current->generation != expected->generation ||
        current->wake_seq != expected->wake_seq ||
        current->request_id != expected->request_id) {
        return false;
    }
    current->generation = 0U;
    current->wake_seq = 0U;
    current->request_id = 0U;
    return true;
}
