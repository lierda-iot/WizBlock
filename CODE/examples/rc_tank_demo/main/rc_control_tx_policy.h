#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "rc_ctrl_protocol.h"

#define RC_CONTROL_TX_HEARTBEAT_MS 100U

typedef struct {
    bool has_sent;
    rc_ctrl_command_t last_sent_command;
    uint32_t last_sent_ms;
    uint16_t next_seq;
} rc_control_tx_policy_t;

static inline bool rc_control_tx_commands_equal(
    const rc_ctrl_command_t *left,
    const rc_ctrl_command_t *right)
{
    if ((NULL == left) || (NULL == right) || (left->mode != right->mode)) {
        return false;
    }
    if (RC_CTRL_MODE_STOP == left->mode) {
        return true;
    }
    return (left->angle_deg == right->angle_deg) &&
           (left->magnitude_pct == right->magnitude_pct);
}

static inline bool rc_control_tx_should_send(
    const rc_control_tx_policy_t *policy,
    const rc_ctrl_command_t *command,
    uint32_t now_ms)
{
    if ((NULL == policy) || (NULL == command)) {
        return false;
    }
    if (!policy->has_sent) {
        return true;
    }
    if (!rc_control_tx_commands_equal(command,
                                      &policy->last_sent_command)) {
        return true;
    }
    return (RC_CTRL_MODE_DRIVE == command->mode) &&
           ((uint32_t)(now_ms - policy->last_sent_ms) >=
            RC_CONTROL_TX_HEARTBEAT_MS);
}

static inline void rc_control_tx_mark_sent(rc_control_tx_policy_t *policy,
                                           const rc_ctrl_command_t *command,
                                           uint32_t now_ms)
{
    if ((NULL == policy) || (NULL == command)) {
        return;
    }
    policy->has_sent = true;
    policy->last_sent_command = *command;
    if (RC_CTRL_MODE_STOP == policy->last_sent_command.mode) {
        policy->last_sent_command.angle_deg = 0;
        policy->last_sent_command.magnitude_pct = 0U;
    }
    policy->last_sent_ms = now_ms;
    ++policy->next_seq;
}

static inline uint16_t rc_control_tx_sequence(
    const rc_control_tx_policy_t *policy)
{
    return (NULL == policy) ? 0U : policy->next_seq;
}

static inline void rc_control_tx_reset(rc_control_tx_policy_t *policy)
{
    if (NULL != policy) {
        *policy = (rc_control_tx_policy_t){0};
    }
}
