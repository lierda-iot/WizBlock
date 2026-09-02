#include "ofdm_crc.h"
#include "ofdm_frame.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void fill_ascii_message(uint8_t *message, size_t length)
{
    assert(NULL != message);
    for (size_t index = 0U; index < length; ++index) {
        message[index] = (uint8_t)('A' + (index % 26U));
    }
}

static void check_frame_count(size_t length, uint8_t expected_count)
{
    uint8_t message[OFDM_MESSAGE_MAX_BYTES] = {0};
    ofdm_frame_t frame = {0};

    fill_ascii_message(message, length);
    assert(OFDM_FRAME_OK == ofdm_frame_build(message, length,
                                              UINT16_C(0x1234), 0U,
                                              &frame));
    assert(expected_count == frame.header.frame_count);
    assert((length < OFDM_FRAME_PAYLOAD_BYTES
                ? length
                : OFDM_FRAME_PAYLOAD_BYTES) == frame.header.payload_len);
}

static void test_message_boundaries(void)
{
    uint8_t message[OFDM_MESSAGE_MAX_BYTES + 1U] = {0};
    ofdm_frame_t frame = {0};

    assert(OFDM_FRAME_INVALID_MESSAGE ==
           ofdm_frame_build(NULL, 0U, 1U, 0U, &frame));
    check_frame_count(1U, 1U);
    check_frame_count(111U, 1U);
    check_frame_count(112U, 1U);
    check_frame_count(113U, 2U);
    check_frame_count(337U, 4U);
    check_frame_count(1024U, 10U);

    fill_ascii_message(message, sizeof(message));
    assert(OFDM_FRAME_INVALID_MESSAGE ==
           ofdm_frame_build(message, sizeof(message), 1U, 0U, &frame));
    assert(OFDM_FRAME_INVALID_SESSION ==
           ofdm_frame_build(message, 1U, 0U, 0U, &frame));
    assert(OFDM_FRAME_INVALID_INDEX ==
           ofdm_frame_build(message, 113U, 1U, 2U, &frame));
}

static void test_header_wire_format(void)
{
    uint8_t message[113U] = {0};
    uint8_t wire[OFDM_FRAME_HEADER_BYTES] = {0};
    ofdm_frame_t frame = {0};
    ofdm_frame_header_t parsed = {0};

    fill_ascii_message(message, sizeof(message));
    assert(OFDM_FRAME_OK ==
           ofdm_frame_build(message, sizeof(message), UINT16_C(0x1234),
                            1U, &frame));
    assert(OFDM_FRAME_OK == ofdm_frame_header_serialize(&frame.header, wire));
    assert(0x4FU == wire[0]);
    assert(0x44U == wire[1]);
    assert(1U == wire[2]);
    assert(OFDM_FRAME_FLAG_LAST == wire[3]);
    assert(0x12U == wire[4]);
    assert(0x34U == wire[5]);
    assert(1U == wire[6]);
    assert(2U == wire[7]);
    assert(1U == wire[8]);
    assert(0U == wire[9]);
    assert(0U == wire[10]);
    assert(113U == wire[11]);
    assert(ofdm_crc16_ccitt_false(wire, OFDM_FRAME_HEADER_CRC_BYTES) ==
           (uint16_t)(((uint16_t)wire[20] << 8U) | wire[21]));
    assert(OFDM_FRAME_OK == ofdm_frame_header_parse(wire, &parsed));
    assert(0 == memcmp(&frame.header, &parsed, sizeof(parsed)));

    wire[7] ^= 1U;
    assert(OFDM_FRAME_HEADER_CRC_FAILED ==
           ofdm_frame_header_parse(wire, &parsed));
}

static void test_last_frame_padding(void)
{
    uint8_t message[337U] = {0};
    ofdm_frame_t frame = {0};

    fill_ascii_message(message, sizeof(message));
    assert(OFDM_FRAME_OK ==
           ofdm_frame_build(message, sizeof(message), 77U, 3U, &frame));
    assert(1U == frame.header.payload_len);
    assert(message[336] == frame.payload[0]);
    for (size_t index = 1U; index < OFDM_FRAME_PAYLOAD_BYTES; ++index) {
        assert(0U == frame.payload[index]);
    }
}

static void test_out_of_order_reassembly(void)
{
    uint8_t message[337U] = {0};
    uint8_t output[OFDM_MESSAGE_MAX_BYTES + 1U] = {0};
    const uint8_t order[] = {2U, 0U, 3U, 1U};
    size_t output_length = 0U;
    ofdm_frame_t frames[4] = {0};
    ofdm_reassembly_t reassembly = {0};

    fill_ascii_message(message, sizeof(message));
    for (uint8_t index = 0U; index < 4U; ++index) {
        assert(OFDM_FRAME_OK ==
               ofdm_frame_build(message, sizeof(message), 91U, index,
                                &frames[index]));
    }
    ofdm_reassembly_reset(&reassembly);
    for (size_t index = 0U; index < sizeof(order); ++index) {
        const ofdm_reassembly_result_t expected =
            index + 1U == sizeof(order) ? OFDM_REASSEMBLY_COMPLETE
                                        : OFDM_REASSEMBLY_ACCEPTED;
        assert(expected == ofdm_reassembly_accept(
                               &reassembly, &frames[order[index]], output,
                               sizeof(output), &output_length));
    }
    assert(sizeof(message) == output_length);
    assert(0 == memcmp(message, output, sizeof(message)));
    assert(0U == output[output_length]);
}

static void test_duplicate_corruption_and_new_session(void)
{
    uint8_t first_message[113U] = {0};
    uint8_t second_message[1U] = {'Z'};
    uint8_t output[OFDM_MESSAGE_MAX_BYTES + 1U] = {0};
    size_t output_length = 0U;
    ofdm_frame_t first[2] = {0};
    ofdm_frame_t second = {0};
    ofdm_reassembly_t reassembly = {0};

    fill_ascii_message(first_message, sizeof(first_message));
    assert(OFDM_FRAME_OK == ofdm_frame_build(first_message,
                                              sizeof(first_message), 10U,
                                              0U, &first[0]));
    assert(OFDM_FRAME_OK == ofdm_frame_build(first_message,
                                              sizeof(first_message), 10U,
                                              1U, &first[1]));
    assert(OFDM_FRAME_OK == ofdm_frame_build(second_message,
                                              sizeof(second_message), 11U,
                                              0U, &second));

    ofdm_reassembly_reset(&reassembly);
    assert(OFDM_REASSEMBLY_ACCEPTED == ofdm_reassembly_accept(
                                             &reassembly, &first[0], output,
                                             sizeof(output), &output_length));
    assert(OFDM_REASSEMBLY_DUPLICATE == ofdm_reassembly_accept(
                                              &reassembly, &first[0], output,
                                              sizeof(output), &output_length));

    first[1].payload[0] ^= 1U;
    assert(OFDM_REASSEMBLY_REJECTED == ofdm_reassembly_accept(
                                             &reassembly, &first[1], output,
                                             sizeof(output), &output_length));

    assert(OFDM_REASSEMBLY_COMPLETE == ofdm_reassembly_accept(
                                             &reassembly, &second, output,
                                             sizeof(output), &output_length));
    assert(1U == output_length);
    assert('Z' == output[0]);
}

static void test_reassembly_received_count(void)
{
    uint8_t message[113U] = {0};
    uint8_t output[OFDM_MESSAGE_MAX_BYTES + 1U] = {0};
    size_t output_length = 0U;
    ofdm_frame_t frames[2] = {0};
    ofdm_reassembly_t reassembly = {0};

    fill_ascii_message(message, sizeof(message));
    assert(0U == ofdm_reassembly_received_count(NULL));
    assert(0U == ofdm_reassembly_received_count(&reassembly));
    assert(OFDM_FRAME_OK == ofdm_frame_build(message, sizeof(message), 12U,
                                              0U, &frames[0]));
    assert(OFDM_FRAME_OK == ofdm_frame_build(message, sizeof(message), 12U,
                                              1U, &frames[1]));
    assert(OFDM_REASSEMBLY_ACCEPTED == ofdm_reassembly_accept(
                                             &reassembly, &frames[1], output,
                                             sizeof(output), &output_length));
    assert(1U == ofdm_reassembly_received_count(&reassembly));
    assert(OFDM_REASSEMBLY_DUPLICATE == ofdm_reassembly_accept(
                                              &reassembly, &frames[1], output,
                                              sizeof(output), &output_length));
    assert(1U == ofdm_reassembly_received_count(&reassembly));
    assert(OFDM_REASSEMBLY_COMPLETE == ofdm_reassembly_accept(
                                             &reassembly, &frames[0], output,
                                             sizeof(output), &output_length));
    assert(0U == ofdm_reassembly_received_count(&reassembly));
}

int main(void)
{
    test_message_boundaries();
    test_header_wire_format();
    test_last_frame_padding();
    test_out_of_order_reassembly();
    test_duplicate_corruption_and_new_session();
    test_reassembly_received_count();
    puts("ofdm_frame_test: PASS");
    return 0;
}
