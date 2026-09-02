#ifndef OFDM_FRAME_H
#define OFDM_FRAME_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ofdm_utf8.h"

#define OFDM_PROTOCOL_MAGIC UINT16_C(0x4F44)
#define OFDM_PROTOCOL_VERSION 1U
#define OFDM_FRAME_FLAG_LAST UINT8_C(0x01)
#define OFDM_FRAME_FLAG_CALIBRATION UINT8_C(0x02)
#define OFDM_FRAME_PAYLOAD_BYTES 112U
#define OFDM_MAX_FRAME_COUNT 10U
#define OFDM_FRAME_HEADER_BYTES 22U
#define OFDM_FRAME_HEADER_CRC_BYTES 20U
#define OFDM_REASSEMBLY_BUFFER_BYTES \
    (OFDM_FRAME_PAYLOAD_BYTES * OFDM_MAX_FRAME_COUNT)

typedef enum {
    OFDM_FRAME_OK = 0,
    OFDM_FRAME_INVALID_ARGUMENT,
    OFDM_FRAME_INVALID_MESSAGE,
    OFDM_FRAME_INVALID_SESSION,
    OFDM_FRAME_INVALID_INDEX,
    OFDM_FRAME_HEADER_CRC_FAILED,
    OFDM_FRAME_HEADER_INVALID,
    OFDM_FRAME_PAYLOAD_CRC_FAILED,
} ofdm_frame_result_t;

typedef struct {
    uint16_t magic;
    uint8_t version;
    uint8_t flags;
    uint16_t session_id;
    uint8_t frame_index;
    uint8_t frame_count;
    uint8_t payload_len;
    uint8_t reserved;
    uint16_t message_len;
    uint32_t payload_crc32;
    uint32_t message_crc32;
    uint16_t header_crc16;
} ofdm_frame_header_t;

typedef struct {
    ofdm_frame_header_t header;
    uint8_t payload[OFDM_FRAME_PAYLOAD_BYTES];
} ofdm_frame_t;

typedef enum {
    OFDM_REASSEMBLY_ACCEPTED = 0,
    OFDM_REASSEMBLY_DUPLICATE,
    OFDM_REASSEMBLY_COMPLETE,
    OFDM_REASSEMBLY_REJECTED,
    OFDM_REASSEMBLY_INVALID_ARGUMENT,
} ofdm_reassembly_result_t;

typedef struct {
    uint8_t payload[OFDM_REASSEMBLY_BUFFER_BYTES];
    uint32_t payload_crc32[OFDM_MAX_FRAME_COUNT];
    uint8_t payload_length[OFDM_MAX_FRAME_COUNT];
    uint32_t message_crc32;
    uint16_t session_id;
    uint16_t message_length;
    uint16_t received_bitmap;
    uint8_t frame_count;
    bool active;
} ofdm_reassembly_t;

ofdm_frame_result_t ofdm_frame_build(const uint8_t *message,
                                     size_t message_length,
                                     uint16_t session_id,
                                     uint8_t frame_index,
                                     ofdm_frame_t *frame);
ofdm_frame_result_t ofdm_frame_build_calibration(
    const uint8_t *payload,
    size_t payload_length,
    uint16_t session_id,
    ofdm_frame_t *frame);
bool ofdm_frame_is_calibration(const ofdm_frame_header_t *header);
ofdm_frame_result_t ofdm_frame_validate(const ofdm_frame_t *frame);
ofdm_frame_result_t ofdm_frame_header_serialize(
    const ofdm_frame_header_t *header,
    uint8_t wire[OFDM_FRAME_HEADER_BYTES]);
ofdm_frame_result_t ofdm_frame_header_parse(
    const uint8_t wire[OFDM_FRAME_HEADER_BYTES],
    ofdm_frame_header_t *header);
uint8_t ofdm_reassembly_received_count(
    const ofdm_reassembly_t *reassembly);
void ofdm_reassembly_reset(ofdm_reassembly_t *reassembly);
ofdm_reassembly_result_t ofdm_reassembly_accept(
    ofdm_reassembly_t *reassembly,
    const ofdm_frame_t *frame,
    uint8_t *output,
    size_t output_capacity,
    size_t *output_length);

#endif
