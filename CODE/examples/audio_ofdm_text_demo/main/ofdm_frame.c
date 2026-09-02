#include "ofdm_frame.h"

#include "ofdm_crc.h"

#include <string.h>

#define UINT16_BYTE_COUNT 2U
#define UINT32_BYTE_COUNT 4U

static void write_u16_be(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8U);
    output[1] = (uint8_t)value;
}

static void write_u32_be(uint8_t *output, uint32_t value)
{
    output[0] = (uint8_t)(value >> 24U);
    output[1] = (uint8_t)(value >> 16U);
    output[2] = (uint8_t)(value >> 8U);
    output[3] = (uint8_t)value;
}

static uint16_t read_u16_be(const uint8_t *input)
{
    return (uint16_t)(((uint16_t)input[0] << 8U) | input[1]);
}

static uint32_t read_u32_be(const uint8_t *input)
{
    return ((uint32_t)input[0] << 24U) |
           ((uint32_t)input[1] << 16U) |
           ((uint32_t)input[2] << 8U) |
           (uint32_t)input[3];
}

static uint8_t calculate_frame_count(size_t message_length)
{
    return (uint8_t)((message_length + OFDM_FRAME_PAYLOAD_BYTES - 1U) /
                     OFDM_FRAME_PAYLOAD_BYTES);
}

static uint8_t calculate_payload_length(uint16_t message_length,
                                        uint8_t frame_index)
{
    const size_t offset = (size_t)frame_index * OFDM_FRAME_PAYLOAD_BYTES;
    const size_t remaining = (size_t)message_length - offset;

    return (uint8_t)((remaining < OFDM_FRAME_PAYLOAD_BYTES)
                         ? remaining
                         : OFDM_FRAME_PAYLOAD_BYTES);
}

static bool is_header_valid(const ofdm_frame_header_t *header)
{
    const bool calibration = NULL != header &&
                             0U != (header->flags &
                                     OFDM_FRAME_FLAG_CALIBRATION);
    if (NULL == header || OFDM_PROTOCOL_MAGIC != header->magic ||
        OFDM_PROTOCOL_VERSION != header->version || 0U == header->session_id ||
        0U != header->reserved ||
        0U != (header->flags & (uint8_t)~(
                    OFDM_FRAME_FLAG_LAST | OFDM_FRAME_FLAG_CALIBRATION)) ||
        0U == header->message_len ||
        OFDM_MESSAGE_MAX_BYTES < header->message_len ||
        0U == header->frame_count ||
        OFDM_MAX_FRAME_COUNT < header->frame_count ||
        header->frame_index >= header->frame_count) {
        return false;
    }

    const uint8_t expected_count = calculate_frame_count(header->message_len);
    const uint8_t expected_payload = calculate_payload_length(
        header->message_len, header->frame_index);
    const bool is_last = header->frame_index + 1U == header->frame_count;
    if (expected_count != header->frame_count ||
        expected_payload != header->payload_len ||
        (is_last ? OFDM_FRAME_FLAG_LAST : 0U) !=
            (header->flags & OFDM_FRAME_FLAG_LAST)) {
        return false;
    }
    if (calibration &&
        (1U != header->frame_count || 0U != header->frame_index ||
         header->payload_len != header->message_len ||
         OFDM_FRAME_PAYLOAD_BYTES < header->payload_len)) {
        return false;
    }
    return true;
}

static void serialize_header_prefix(const ofdm_frame_header_t *header,
                                    uint8_t *wire)
{
    write_u16_be(&wire[0], header->magic);
    wire[2] = header->version;
    wire[3] = header->flags;
    write_u16_be(&wire[4], header->session_id);
    wire[6] = header->frame_index;
    wire[7] = header->frame_count;
    wire[8] = header->payload_len;
    wire[9] = header->reserved;
    write_u16_be(&wire[10], header->message_len);
    write_u32_be(&wire[12], header->payload_crc32);
    write_u32_be(&wire[16], header->message_crc32);
}

static uint16_t calculate_header_crc(const ofdm_frame_header_t *header)
{
    uint8_t wire[OFDM_FRAME_HEADER_CRC_BYTES] = {0};

    serialize_header_prefix(header, wire);
    return ofdm_crc16_ccitt_false(wire, sizeof(wire));
}

ofdm_frame_result_t ofdm_frame_validate(const ofdm_frame_t *frame)
{
    if (NULL == frame) {
        return OFDM_FRAME_INVALID_ARGUMENT;
    }
    if (!is_header_valid(&frame->header) ||
        calculate_header_crc(&frame->header) != frame->header.header_crc16) {
        return OFDM_FRAME_HEADER_INVALID;
    }
    if (ofdm_crc32_iso_hdlc(frame->payload, frame->header.payload_len) !=
        frame->header.payload_crc32) {
        return OFDM_FRAME_PAYLOAD_CRC_FAILED;
    }
    for (size_t index = frame->header.payload_len;
         index < OFDM_FRAME_PAYLOAD_BYTES; ++index) {
        if (0U != frame->payload[index]) {
            return OFDM_FRAME_PAYLOAD_CRC_FAILED;
        }
    }
    return OFDM_FRAME_OK;
}

ofdm_frame_result_t ofdm_frame_build(const uint8_t *message,
                                     size_t message_length,
                                     uint16_t session_id,
                                     uint8_t frame_index,
                                     ofdm_frame_t *frame)
{
    if (NULL == frame) {
        return OFDM_FRAME_INVALID_ARGUMENT;
    }
    if (OFDM_MESSAGE_OK != ofdm_message_validate(message, message_length)) {
        return OFDM_FRAME_INVALID_MESSAGE;
    }
    if (0U == session_id) {
        return OFDM_FRAME_INVALID_SESSION;
    }

    const uint8_t frame_count = calculate_frame_count(message_length);
    if (frame_index >= frame_count) {
        return OFDM_FRAME_INVALID_INDEX;
    }

    memset(frame, 0, sizeof(*frame));
    const size_t payload_offset =
        (size_t)frame_index * OFDM_FRAME_PAYLOAD_BYTES;
    const uint8_t payload_length = calculate_payload_length(
        (uint16_t)message_length, frame_index);
    memcpy(frame->payload, &message[payload_offset], payload_length);

    frame->header.magic = OFDM_PROTOCOL_MAGIC;
    frame->header.version = OFDM_PROTOCOL_VERSION;
    frame->header.flags = (frame_index + 1U == frame_count)
                              ? OFDM_FRAME_FLAG_LAST
                              : 0U;
    frame->header.session_id = session_id;
    frame->header.frame_index = frame_index;
    frame->header.frame_count = frame_count;
    frame->header.payload_len = payload_length;
    frame->header.reserved = 0U;
    frame->header.message_len = (uint16_t)message_length;
    frame->header.payload_crc32 = ofdm_crc32_iso_hdlc(frame->payload,
                                                       payload_length);
    frame->header.message_crc32 = ofdm_crc32_iso_hdlc(message,
                                                       message_length);
    frame->header.header_crc16 = calculate_header_crc(&frame->header);
    return OFDM_FRAME_OK;
}

ofdm_frame_result_t ofdm_frame_build_calibration(
    const uint8_t *payload,
    size_t payload_length,
    uint16_t session_id,
    ofdm_frame_t *frame)
{
    if (NULL == payload || NULL == frame) {
        return OFDM_FRAME_INVALID_ARGUMENT;
    }
    if (0U == session_id || 0U == payload_length ||
        OFDM_FRAME_PAYLOAD_BYTES < payload_length) {
        return OFDM_FRAME_INVALID_MESSAGE;
    }

    memset(frame, 0, sizeof(*frame));
    memcpy(frame->payload, payload, payload_length);
    frame->header.magic = OFDM_PROTOCOL_MAGIC;
    frame->header.version = OFDM_PROTOCOL_VERSION;
    frame->header.flags = OFDM_FRAME_FLAG_LAST |
                          OFDM_FRAME_FLAG_CALIBRATION;
    frame->header.session_id = session_id;
    frame->header.frame_index = 0U;
    frame->header.frame_count = 1U;
    frame->header.payload_len = (uint8_t)payload_length;
    frame->header.reserved = 0U;
    frame->header.message_len = (uint16_t)payload_length;
    frame->header.payload_crc32 = ofdm_crc32_iso_hdlc(
        frame->payload, payload_length);
    frame->header.message_crc32 = frame->header.payload_crc32;
    frame->header.header_crc16 = calculate_header_crc(&frame->header);
    return OFDM_FRAME_OK;
}

bool ofdm_frame_is_calibration(const ofdm_frame_header_t *header)
{
    return NULL != header &&
           0U != (header->flags & OFDM_FRAME_FLAG_CALIBRATION);
}

ofdm_frame_result_t ofdm_frame_header_serialize(
    const ofdm_frame_header_t *header,
    uint8_t wire[OFDM_FRAME_HEADER_BYTES])
{
    if (NULL == header || NULL == wire) {
        return OFDM_FRAME_INVALID_ARGUMENT;
    }
    if (!is_header_valid(header)) {
        return OFDM_FRAME_HEADER_INVALID;
    }
    serialize_header_prefix(header, wire);
    write_u16_be(&wire[OFDM_FRAME_HEADER_CRC_BYTES],
                 ofdm_crc16_ccitt_false(wire, OFDM_FRAME_HEADER_CRC_BYTES));
    return OFDM_FRAME_OK;
}

ofdm_frame_result_t ofdm_frame_header_parse(
    const uint8_t wire[OFDM_FRAME_HEADER_BYTES],
    ofdm_frame_header_t *header)
{
    if (NULL == wire || NULL == header) {
        return OFDM_FRAME_INVALID_ARGUMENT;
    }
    const uint16_t expected_crc = ofdm_crc16_ccitt_false(
        wire, OFDM_FRAME_HEADER_CRC_BYTES);
    const uint16_t received_crc = read_u16_be(
        &wire[OFDM_FRAME_HEADER_CRC_BYTES]);
    if (expected_crc != received_crc) {
        return OFDM_FRAME_HEADER_CRC_FAILED;
    }

    memset(header, 0, sizeof(*header));
    header->magic = read_u16_be(&wire[0]);
    header->version = wire[2];
    header->flags = wire[3];
    header->session_id = read_u16_be(&wire[4]);
    header->frame_index = wire[6];
    header->frame_count = wire[7];
    header->payload_len = wire[8];
    header->reserved = wire[9];
    header->message_len = read_u16_be(&wire[10]);
    header->payload_crc32 = read_u32_be(&wire[12]);
    header->message_crc32 = read_u32_be(&wire[16]);
    header->header_crc16 = received_crc;
    return is_header_valid(header) ? OFDM_FRAME_OK
                                   : OFDM_FRAME_HEADER_INVALID;
}

void ofdm_reassembly_reset(ofdm_reassembly_t *reassembly)
{
    if (NULL != reassembly) {
        memset(reassembly, 0, sizeof(*reassembly));
    }
}

uint8_t ofdm_reassembly_received_count(
    const ofdm_reassembly_t *reassembly)
{
    if (NULL == reassembly || !reassembly->active) {
        return 0U;
    }

    uint16_t received_bitmap = reassembly->received_bitmap;
    uint8_t received_count = 0U;
    while (0U != received_bitmap) {
        received_count = (uint8_t)(received_count +
                                   (uint8_t)(received_bitmap & 1U));
        received_bitmap >>= 1U;
    }
    return received_count;
}

static void start_reassembly(ofdm_reassembly_t *reassembly,
                             const ofdm_frame_header_t *header)
{
    ofdm_reassembly_reset(reassembly);
    reassembly->active = true;
    reassembly->session_id = header->session_id;
    reassembly->frame_count = header->frame_count;
    reassembly->message_length = header->message_len;
    reassembly->message_crc32 = header->message_crc32;
}

static bool metadata_matches(const ofdm_reassembly_t *reassembly,
                             const ofdm_frame_header_t *header)
{
    return reassembly->session_id == header->session_id &&
           reassembly->frame_count == header->frame_count &&
           reassembly->message_length == header->message_len &&
           reassembly->message_crc32 == header->message_crc32;
}

ofdm_reassembly_result_t ofdm_reassembly_accept(
    ofdm_reassembly_t *reassembly,
    const ofdm_frame_t *frame,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length)
{
    if (NULL == reassembly || NULL == frame || NULL == output ||
        NULL == output_length) {
        return OFDM_REASSEMBLY_INVALID_ARGUMENT;
    }
    *output_length = 0U;
    if (ofdm_frame_is_calibration(&frame->header) ||
        OFDM_FRAME_OK != ofdm_frame_validate(frame) ||
        output_capacity <= frame->header.message_len) {
        ofdm_reassembly_reset(reassembly);
        return OFDM_REASSEMBLY_REJECTED;
    }

    if (!reassembly->active ||
        reassembly->session_id != frame->header.session_id) {
        start_reassembly(reassembly, &frame->header);
    } else if (!metadata_matches(reassembly, &frame->header)) {
        ofdm_reassembly_reset(reassembly);
        return OFDM_REASSEMBLY_REJECTED;
    }

    const uint16_t frame_bit = (uint16_t)(1U << frame->header.frame_index);
    if (0U != (reassembly->received_bitmap & frame_bit)) {
        if (reassembly->payload_crc32[frame->header.frame_index] ==
                frame->header.payload_crc32 &&
            reassembly->payload_length[frame->header.frame_index] ==
                frame->header.payload_len) {
            return OFDM_REASSEMBLY_DUPLICATE;
        }
        ofdm_reassembly_reset(reassembly);
        return OFDM_REASSEMBLY_REJECTED;
    }

    const size_t payload_offset =
        (size_t)frame->header.frame_index * OFDM_FRAME_PAYLOAD_BYTES;
    memcpy(&reassembly->payload[payload_offset], frame->payload,
           frame->header.payload_len);
    reassembly->payload_crc32[frame->header.frame_index] =
        frame->header.payload_crc32;
    reassembly->payload_length[frame->header.frame_index] =
        frame->header.payload_len;
    reassembly->received_bitmap |= frame_bit;

    const uint16_t complete_bitmap = (uint16_t)(
        (UINT16_C(1) << reassembly->frame_count) - UINT16_C(1));
    if (complete_bitmap != reassembly->received_bitmap) {
        return OFDM_REASSEMBLY_ACCEPTED;
    }

    size_t assembled_length = 0U;
    for (uint8_t index = 0U; index < reassembly->frame_count; ++index) {
        assembled_length += reassembly->payload_length[index];
    }
    if (assembled_length != reassembly->message_length ||
        ofdm_crc32_iso_hdlc(reassembly->payload, assembled_length) !=
            reassembly->message_crc32 ||
        OFDM_MESSAGE_OK !=
            ofdm_message_validate(reassembly->payload, assembled_length)) {
        ofdm_reassembly_reset(reassembly);
        return OFDM_REASSEMBLY_REJECTED;
    }

    memcpy(output, reassembly->payload, assembled_length);
    output[assembled_length] = 0U;
    *output_length = assembled_length;
    ofdm_reassembly_reset(reassembly);
    return OFDM_REASSEMBLY_COMPLETE;
}
