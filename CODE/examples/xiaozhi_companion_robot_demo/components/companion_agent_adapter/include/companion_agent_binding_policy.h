#pragma once

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    uint32_t generation;
    uint32_t wake_seq;
    uint32_t request_id;
} companion_agent_binding_id_t;

typedef enum {
    COMPANION_AGENT_BINDING_DROP = 0,
    COMPANION_AGENT_BINDING_CURRENT,
} companion_agent_binding_route_t;

companion_agent_binding_route_t companion_agent_binding_route_audio(
    const companion_agent_binding_id_t *current,
    uint32_t event_request_id);
bool companion_agent_binding_retire_if_current(
    companion_agent_binding_id_t *current,
    const companion_agent_binding_id_t *expected);
