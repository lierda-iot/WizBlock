#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rc_tank_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC_VIDEO_UDP_CHUNK_DATA_MAX 1200U
#define RC_VIDEO_UDP_MAX_CHUNKS 64U

typedef enum {
    RC_VIDEO_UDP_FRAME_REJECTED = -1,
    RC_VIDEO_UDP_FRAME_INCOMPLETE = 0,
    RC_VIDEO_UDP_FRAME_COMPLETE = 1,
} rc_video_udp_push_result_t;

typedef struct {
    bool has_latest_seq;
    bool active;
    uint16_t latest_seq;
    uint8_t chunk_count;
    uint64_t received_mask;
    size_t final_chunk_len;
} rc_video_udp_reassembly_t;

void rc_video_udp_reassembly_reset(rc_video_udp_reassembly_t *state);

uint8_t rc_video_udp_chunk_count(size_t frame_len);
size_t rc_video_udp_chunk_payload_len(size_t frame_len, uint8_t chunk_index);
uint16_t rc_video_udp_pack_chunk_meta(uint8_t chunk_index, uint8_t chunk_count);
bool rc_video_udp_unpack_chunk_meta(uint16_t reserved,
                                    uint8_t *chunk_index,
                                    uint8_t *chunk_count);

rc_video_udp_push_result_t rc_video_udp_reassembly_push(
    rc_video_udp_reassembly_t *state,
    const rc_video_header_t *header,
    const uint8_t *payload,
    size_t payload_len,
    uint8_t *frame_buf,
    size_t frame_capacity,
    size_t *out_frame_len,
    uint16_t *out_frame_seq);

#ifdef __cplusplus
}
#endif
