#include <stdint.h>

#include "rc_ctrl_protocol.h"

#define TEST_ASSERT(condition) do { if (!(condition)) return __LINE__; } while (0)

static int test_drive_packet_encodes_exact_v1_wire_bytes(void)
{
    const rc_ctrl_packet_t packet = {
        .mode = RC_CTRL_MODE_DRIVE,
        .angle_deg = -45,
        .magnitude_pct = 100U,
        .seq = 0x1234U,
        .sender_time_ms = 0x01020304U,
    };
    uint8_t wire[RC_CTRL_PACKET_SIZE] = {0};
    const uint8_t expected[RC_CTRL_PACKET_SIZE] = {
        0x52U, 0x43U, 0x01U, 0x01U,
        0xFFU, 0xD3U, 0x64U, 0x00U,
        0x12U, 0x34U, 0x01U, 0x02U,
        0x03U, 0x04U,
    };

    TEST_ASSERT(rc_ctrl_packet_encode(&packet, wire, sizeof(wire)));
    for (uint32_t index = 0U; index < RC_CTRL_PACKET_SIZE; ++index) {
        TEST_ASSERT(expected[index] == wire[index]);
    }
    return 0;
}

static int test_drive_packet_decodes_from_exact_v1_wire_bytes(void)
{
    const uint8_t wire[RC_CTRL_PACKET_SIZE] = {
        0x52U, 0x43U, 0x01U, 0x01U,
        0xFFU, 0xD3U, 0x64U, 0x00U,
        0x12U, 0x34U, 0x01U, 0x02U,
        0x03U, 0x04U,
    };
    rc_ctrl_packet_t packet = {0};

    TEST_ASSERT(rc_ctrl_packet_decode(wire, sizeof(wire), &packet));
    TEST_ASSERT(RC_CTRL_MODE_DRIVE == packet.mode);
    TEST_ASSERT(-45 == packet.angle_deg);
    TEST_ASSERT(100U == packet.magnitude_pct);
    TEST_ASSERT(0x1234U == packet.seq);
    TEST_ASSERT(0x01020304U == packet.sender_time_ms);
    return 0;
}

static int test_stop_packet_canonicalizes_angle_and_magnitude(void)
{
    const rc_ctrl_packet_t stop = {
        .mode = RC_CTRL_MODE_STOP,
        .angle_deg = 90,
        .magnitude_pct = 75U,
        .seq = 9U,
        .sender_time_ms = 20U,
    };
    uint8_t wire[RC_CTRL_PACKET_SIZE] = {0};
    rc_ctrl_packet_t decoded = {0};

    TEST_ASSERT(rc_ctrl_packet_encode(&stop, wire, sizeof(wire)));
    TEST_ASSERT(0U == wire[4] && 0U == wire[5] && 0U == wire[6]);

    wire[4] = 0xFFU;
    wire[5] = 0xA6U;
    wire[6] = 33U;
    TEST_ASSERT(rc_ctrl_packet_decode(wire, sizeof(wire), &decoded));
    TEST_ASSERT(RC_CTRL_MODE_STOP == decoded.mode);
    TEST_ASSERT(0 == decoded.angle_deg);
    TEST_ASSERT(0U == decoded.magnitude_pct);
    return 0;
}

static int test_decode_rejects_invalid_envelope_without_touching_output(void)
{
    uint8_t wire[RC_CTRL_PACKET_SIZE] = {
        0x52U, 0x43U, 0x01U, 0x01U,
        0x00U, 0x2DU, 0x32U, 0x00U,
        0x00U, 0x01U, 0x00U, 0x00U,
        0x00U, 0x02U,
    };
    const rc_ctrl_packet_t sentinel = {
        .mode = RC_CTRL_MODE_STOP,
        .angle_deg = 7,
        .magnitude_pct = 8U,
        .seq = 9U,
        .sender_time_ms = 10U,
    };
    rc_ctrl_packet_t output = sentinel;

    TEST_ASSERT(!rc_ctrl_packet_decode(wire, RC_CTRL_PACKET_SIZE - 1U, &output));
    TEST_ASSERT(!rc_ctrl_packet_decode(wire, RC_CTRL_PACKET_SIZE + 1U, &output));

    wire[0] = 0U;
    TEST_ASSERT(!rc_ctrl_packet_decode(wire, sizeof(wire), &output));
    wire[0] = 0x52U;
    wire[2] = 2U;
    TEST_ASSERT(!rc_ctrl_packet_decode(wire, sizeof(wire), &output));
    wire[2] = RC_CTRL_VERSION;
    wire[7] = 1U;
    TEST_ASSERT(!rc_ctrl_packet_decode(wire, sizeof(wire), &output));
    wire[7] = 0U;
    wire[3] = 2U;
    TEST_ASSERT(!rc_ctrl_packet_decode(wire, sizeof(wire), &output));

    TEST_ASSERT(sentinel.mode == output.mode);
    TEST_ASSERT(sentinel.angle_deg == output.angle_deg);
    TEST_ASSERT(sentinel.magnitude_pct == output.magnitude_pct);
    TEST_ASSERT(sentinel.seq == output.seq);
    TEST_ASSERT(sentinel.sender_time_ms == output.sender_time_ms);
    return 0;
}

static void set_wire_angle(uint8_t *wire, int16_t angle_deg)
{
    const uint16_t angle_bits = (uint16_t)angle_deg;
    wire[4] = (uint8_t)(angle_bits >> 8);
    wire[5] = (uint8_t)angle_bits;
}

static int test_decode_enforces_drive_angle_and_magnitude_domains(void)
{
    uint8_t wire[RC_CTRL_PACKET_SIZE] = {
        0x52U, 0x43U, 0x01U, 0x01U,
        0x00U, 0x00U, 0x01U, 0x00U,
        0x00U, 0x01U, 0x00U, 0x00U,
        0x00U, 0x02U,
    };
    rc_ctrl_packet_t output = {0};

    set_wire_angle(wire, -180);
    TEST_ASSERT(rc_ctrl_packet_decode(wire, sizeof(wire), &output));
    set_wire_angle(wire, 180);
    wire[6] = 100U;
    TEST_ASSERT(rc_ctrl_packet_decode(wire, sizeof(wire), &output));

    set_wire_angle(wire, -181);
    TEST_ASSERT(!rc_ctrl_packet_decode(wire, sizeof(wire), &output));
    set_wire_angle(wire, 181);
    TEST_ASSERT(!rc_ctrl_packet_decode(wire, sizeof(wire), &output));
    set_wire_angle(wire, 0);
    wire[6] = 0U;
    TEST_ASSERT(!rc_ctrl_packet_decode(wire, sizeof(wire), &output));
    wire[6] = 101U;
    TEST_ASSERT(!rc_ctrl_packet_decode(wire, sizeof(wire), &output));
    return 0;
}

static int test_encode_rejects_invalid_inputs_without_writing_wire(void)
{
    rc_ctrl_packet_t packet = {
        .mode = (rc_ctrl_mode_t)2,
        .angle_deg = 0,
        .magnitude_pct = 50U,
        .seq = 1U,
        .sender_time_ms = 2U,
    };
    uint8_t wire[RC_CTRL_PACKET_SIZE] = {0xA5U};

    TEST_ASSERT(!rc_ctrl_packet_encode(NULL, wire, sizeof(wire)));
    TEST_ASSERT(!rc_ctrl_packet_encode(&packet, NULL, sizeof(wire)));
    TEST_ASSERT(!rc_ctrl_packet_encode(&packet, wire, RC_CTRL_PACKET_SIZE - 1U));
    TEST_ASSERT(!rc_ctrl_packet_encode(&packet, wire, sizeof(wire)));
    TEST_ASSERT(0xA5U == wire[0]);

    packet.mode = RC_CTRL_MODE_DRIVE;
    packet.angle_deg = -181;
    TEST_ASSERT(!rc_ctrl_packet_encode(&packet, wire, sizeof(wire)));
    packet.angle_deg = 181;
    TEST_ASSERT(!rc_ctrl_packet_encode(&packet, wire, sizeof(wire)));
    packet.angle_deg = 0;
    packet.magnitude_pct = 0U;
    TEST_ASSERT(!rc_ctrl_packet_encode(&packet, wire, sizeof(wire)));
    packet.magnitude_pct = 101U;
    TEST_ASSERT(!rc_ctrl_packet_encode(&packet, wire, sizeof(wire)));
    return 0;
}

static int test_sequence_newness_uses_forward_half_range_with_wraparound(void)
{
    TEST_ASSERT(!rc_ctrl_seq_is_newer(100U, 100U));
    TEST_ASSERT(rc_ctrl_seq_is_newer(101U, 100U));
    TEST_ASSERT(rc_ctrl_seq_is_newer((uint16_t)(100U + 32767U), 100U));
    TEST_ASSERT(!rc_ctrl_seq_is_newer((uint16_t)(100U + 32768U), 100U));
    TEST_ASSERT(!rc_ctrl_seq_is_newer(99U, 100U));
    TEST_ASSERT(rc_ctrl_seq_is_newer(0U, 65535U));
    TEST_ASSERT(!rc_ctrl_seq_is_newer(65535U, 0U));
    return 0;
}

static int test_receiver_only_commits_valid_new_packets_and_resets_baseline(void)
{
    rc_ctrl_receiver_t receiver = {0};
    rc_ctrl_packet_t packet = {
        .mode = RC_CTRL_MODE_DRIVE,
        .angle_deg = 30,
        .magnitude_pct = 50U,
        .seq = 100U,
        .sender_time_ms = 1U,
    };
    rc_ctrl_command_t command = {
        .mode = RC_CTRL_MODE_STOP,
        .angle_deg = 0,
        .magnitude_pct = 0U,
    };
    uint8_t wire[RC_CTRL_PACKET_SIZE] = {0};

    rc_ctrl_receiver_reset(&receiver);
    TEST_ASSERT(rc_ctrl_packet_encode(&packet, wire, sizeof(wire)));
    wire[0] = 0U;
    TEST_ASSERT(!rc_ctrl_receiver_accept_wire(&receiver, wire, sizeof(wire),
                                              10U, &command));
    TEST_ASSERT(!rc_ctrl_receiver_is_timed_out(&receiver, 1000U));

    TEST_ASSERT(rc_ctrl_packet_encode(&packet, wire, sizeof(wire)));
    TEST_ASSERT(rc_ctrl_receiver_accept_wire(&receiver, wire, sizeof(wire),
                                             20U, &command));
    TEST_ASSERT(RC_CTRL_MODE_DRIVE == command.mode);
    TEST_ASSERT(30 == command.angle_deg && 50U == command.magnitude_pct);
    TEST_ASSERT(!rc_ctrl_receiver_is_timed_out(&receiver, 320U));
    TEST_ASSERT(rc_ctrl_receiver_is_timed_out(&receiver, 321U));

    command.angle_deg = 77;
    TEST_ASSERT(!rc_ctrl_receiver_accept_wire(&receiver, wire, sizeof(wire),
                                              300U, &command));
    TEST_ASSERT(77 == command.angle_deg);
    packet.seq = 99U;
    TEST_ASSERT(rc_ctrl_packet_encode(&packet, wire, sizeof(wire)));
    TEST_ASSERT(!rc_ctrl_receiver_accept_wire(&receiver, wire, sizeof(wire),
                                              301U, &command));
    TEST_ASSERT(rc_ctrl_receiver_is_timed_out(&receiver, 321U));

    rc_ctrl_receiver_reset(&receiver);
    TEST_ASSERT(rc_ctrl_receiver_accept_wire(&receiver, wire, sizeof(wire),
                                             400U, &command));
    TEST_ASSERT(!rc_ctrl_receiver_is_timed_out(&receiver, 700U));
    return 0;
}

int main(void)
{
    int result = test_drive_packet_encodes_exact_v1_wire_bytes();
    if (0 != result) {
        return result;
    }
    result = test_drive_packet_decodes_from_exact_v1_wire_bytes();
    if (0 != result) {
        return result;
    }
    result = test_stop_packet_canonicalizes_angle_and_magnitude();
    if (0 != result) {
        return result;
    }
    result = test_decode_rejects_invalid_envelope_without_touching_output();
    if (0 != result) {
        return result;
    }
    result = test_decode_enforces_drive_angle_and_magnitude_domains();
    if (0 != result) {
        return result;
    }
    result = test_encode_rejects_invalid_inputs_without_writing_wire();
    if (0 != result) {
        return result;
    }
    result = test_sequence_newness_uses_forward_half_range_with_wraparound();
    if (0 != result) {
        return result;
    }
    return test_receiver_only_commits_valid_new_packets_and_resets_baseline();
}
