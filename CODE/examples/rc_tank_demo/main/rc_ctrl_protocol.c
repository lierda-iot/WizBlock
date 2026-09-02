#include "rc_ctrl_protocol.h"

static bool command_fields_are_valid(const rc_ctrl_packet_t *packet)
{
    if (RC_CTRL_MODE_STOP == packet->mode) {
        return true;
    }
    if (RC_CTRL_MODE_DRIVE != packet->mode) {
        return false;
    }
    return (RC_CTRL_ANGLE_MIN_DEG <= packet->angle_deg) &&
           (RC_CTRL_ANGLE_MAX_DEG >= packet->angle_deg) &&
           (RC_CTRL_MAGNITUDE_MIN <= packet->magnitude_pct) &&
           (RC_CTRL_MAGNITUDE_MAX >= packet->magnitude_pct);
}

bool rc_ctrl_packet_encode(const rc_ctrl_packet_t *packet,
                           uint8_t *wire,
                           size_t wire_size)
{
    uint16_t angle_bits = 0U;
    uint8_t magnitude_pct = 0U;

    if ((NULL == packet) || (NULL == wire) ||
        (RC_CTRL_PACKET_SIZE > wire_size)) {
        return false;
    }
    if (!command_fields_are_valid(packet)) {
        return false;
    }
    if (RC_CTRL_MODE_DRIVE == packet->mode) {
        angle_bits = (uint16_t)packet->angle_deg;
        magnitude_pct = packet->magnitude_pct;
    }

    wire[0] = (uint8_t)(RC_CTRL_MAGIC >> 8);
    wire[1] = (uint8_t)RC_CTRL_MAGIC;
    wire[2] = RC_CTRL_VERSION;
    wire[3] = (uint8_t)packet->mode;
    wire[4] = (uint8_t)(angle_bits >> 8);
    wire[5] = (uint8_t)angle_bits;
    wire[6] = magnitude_pct;
    wire[7] = 0U;
    wire[8] = (uint8_t)(packet->seq >> 8);
    wire[9] = (uint8_t)packet->seq;
    wire[10] = (uint8_t)(packet->sender_time_ms >> 24);
    wire[11] = (uint8_t)(packet->sender_time_ms >> 16);
    wire[12] = (uint8_t)(packet->sender_time_ms >> 8);
    wire[13] = (uint8_t)packet->sender_time_ms;
    return true;
}

bool rc_ctrl_packet_decode(const uint8_t *wire,
                           size_t wire_size,
                           rc_ctrl_packet_t *packet)
{
    uint16_t angle_bits = 0U;
    rc_ctrl_packet_t decoded = {0};

    if ((NULL == wire) || (NULL == packet) ||
        (RC_CTRL_PACKET_SIZE != wire_size)) {
        return false;
    }
    if (((uint8_t)(RC_CTRL_MAGIC >> 8) != wire[0]) ||
        ((uint8_t)RC_CTRL_MAGIC != wire[1]) ||
        (RC_CTRL_VERSION != wire[2]) || (0U != wire[7]) ||
        ((RC_CTRL_MODE_STOP != (rc_ctrl_mode_t)wire[3]) &&
         (RC_CTRL_MODE_DRIVE != (rc_ctrl_mode_t)wire[3]))) {
        return false;
    }

    angle_bits = (uint16_t)(((uint16_t)wire[4] << 8) | wire[5]);
    decoded.mode = (rc_ctrl_mode_t)wire[3];
    decoded.angle_deg = (int16_t)angle_bits;
    decoded.magnitude_pct = wire[6];
    decoded.seq = (uint16_t)(((uint16_t)wire[8] << 8) | wire[9]);
    decoded.sender_time_ms = ((uint32_t)wire[10] << 24) |
                             ((uint32_t)wire[11] << 16) |
                             ((uint32_t)wire[12] << 8) |
                             wire[13];
    if (!command_fields_are_valid(&decoded)) {
        return false;
    }
    if (RC_CTRL_MODE_STOP == decoded.mode) {
        decoded.angle_deg = 0;
        decoded.magnitude_pct = 0U;
    }
    *packet = decoded;
    return true;
}

bool rc_ctrl_seq_is_newer(uint16_t new_seq, uint16_t last_seq)
{
    const uint16_t delta = (uint16_t)(new_seq - last_seq);
    return (0U < delta) && (RC_CTRL_SEQ_NEWER_MAX_DELTA >= delta);
}

void rc_ctrl_receiver_reset(rc_ctrl_receiver_t *receiver)
{
    if (NULL != receiver) {
        *receiver = (rc_ctrl_receiver_t){0};
    }
}

bool rc_ctrl_receiver_accept_wire(rc_ctrl_receiver_t *receiver,
                                  const uint8_t *wire,
                                  size_t wire_size,
                                  uint32_t now_ms,
                                  rc_ctrl_command_t *command)
{
    rc_ctrl_packet_t packet = {0};
    rc_ctrl_command_t accepted_command = {0};

    if ((NULL == receiver) || (NULL == command) ||
        !rc_ctrl_packet_decode(wire, wire_size, &packet)) {
        return false;
    }
    if (receiver->has_sequence &&
        !rc_ctrl_seq_is_newer(packet.seq, receiver->last_seq)) {
        return false;
    }

    accepted_command.mode = packet.mode;
    accepted_command.angle_deg = packet.angle_deg;
    accepted_command.magnitude_pct = packet.magnitude_pct;
    receiver->has_sequence = true;
    receiver->last_seq = packet.seq;
    receiver->last_valid_ms = now_ms;
    *command = accepted_command;
    return true;
}

bool rc_ctrl_receiver_is_timed_out(const rc_ctrl_receiver_t *receiver,
                                   uint32_t now_ms)
{
    if ((NULL == receiver) || !receiver->has_sequence) {
        return false;
    }
    return (uint32_t)(now_ms - receiver->last_valid_ms) >
           RC_CTRL_RECEIVER_TIMEOUT_MS;
}
