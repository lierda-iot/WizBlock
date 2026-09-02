#include "rc_video_udp_transport.h"

#include <string.h>

void rc_video_udp_reassembly_reset(rc_video_udp_reassembly_t *state)
{
    if (NULL != state) {
        memset(state, 0, sizeof(*state));
    }
}

uint8_t rc_video_udp_chunk_count(size_t frame_len)
{
    if (0U == frame_len) {
        return 0U;
    }

    const size_t chunk_count =
        (frame_len + RC_VIDEO_UDP_CHUNK_DATA_MAX - 1U) /
        RC_VIDEO_UDP_CHUNK_DATA_MAX;
    if (chunk_count > RC_VIDEO_UDP_MAX_CHUNKS) {
        return 0U;
    }
    return (uint8_t)chunk_count;
}

size_t rc_video_udp_chunk_payload_len(size_t frame_len, uint8_t chunk_index)
{
    const uint8_t chunk_count = rc_video_udp_chunk_count(frame_len);
    if (0U == chunk_count || chunk_index >= chunk_count) {
        return 0U;
    }

    const size_t offset = (size_t)chunk_index * RC_VIDEO_UDP_CHUNK_DATA_MAX;
    const size_t remaining = frame_len - offset;
    return remaining < RC_VIDEO_UDP_CHUNK_DATA_MAX
        ? remaining : RC_VIDEO_UDP_CHUNK_DATA_MAX;
}

uint16_t rc_video_udp_pack_chunk_meta(uint8_t chunk_index, uint8_t chunk_count)
{
    if (0U == chunk_count || chunk_count > RC_VIDEO_UDP_MAX_CHUNKS ||
        chunk_index >= chunk_count) {
        return 0U;
    }
    return (uint16_t)(((uint16_t)chunk_count << 8) | chunk_index);
}

bool rc_video_udp_unpack_chunk_meta(uint16_t reserved,
                                    uint8_t *chunk_index,
                                    uint8_t *chunk_count)
{
    if (NULL == chunk_index || NULL == chunk_count) {
        return false;
    }

    const uint8_t index = (uint8_t)(reserved & 0xFFU);
    const uint8_t count = (uint8_t)(reserved >> 8);
    if (0U == count || count > RC_VIDEO_UDP_MAX_CHUNKS || index >= count) {
        return false;
    }

    *chunk_index = index;
    *chunk_count = count;
    return true;
}

static bool seq_is_newer(uint16_t candidate, uint16_t reference)
{
    return (int16_t)(candidate - reference) > 0;
}

static uint64_t expected_mask(uint8_t chunk_count)
{
    return RC_VIDEO_UDP_MAX_CHUNKS == chunk_count
        ? UINT64_MAX : ((UINT64_C(1) << chunk_count) - UINT64_C(1));
}

rc_video_udp_push_result_t rc_video_udp_reassembly_push(
    rc_video_udp_reassembly_t *state,
    const rc_video_header_t *header,
    const uint8_t *payload,
    size_t payload_len,
    uint8_t *frame_buf,
    size_t frame_capacity,
    size_t *out_frame_len,
    uint16_t *out_frame_seq)
{
    if (NULL == state || NULL == header || NULL == payload ||
        NULL == frame_buf || NULL == out_frame_len || NULL == out_frame_seq ||
        RC_VIDEO_MAGIC != header->magic || header->length != payload_len ||
        0U == payload_len || payload_len > RC_VIDEO_UDP_CHUNK_DATA_MAX) {
        return RC_VIDEO_UDP_FRAME_REJECTED;
    }

    uint8_t chunk_index = 0U;
    uint8_t chunk_count = 0U;
    if (!rc_video_udp_unpack_chunk_meta(header->reserved,
                                        &chunk_index, &chunk_count)) {
        return RC_VIDEO_UDP_FRAME_REJECTED;
    }

    const bool is_final = chunk_index == (uint8_t)(chunk_count - 1U);
    if ((!is_final && RC_VIDEO_UDP_CHUNK_DATA_MAX != payload_len) ||
        (is_final && payload_len > RC_VIDEO_UDP_CHUNK_DATA_MAX)) {
        return RC_VIDEO_UDP_FRAME_REJECTED;
    }

    const size_t offset = (size_t)chunk_index * RC_VIDEO_UDP_CHUNK_DATA_MAX;
    if (offset > frame_capacity || payload_len > frame_capacity - offset) {
        return RC_VIDEO_UDP_FRAME_REJECTED;
    }

    if (!state->has_latest_seq || seq_is_newer(header->seq, state->latest_seq)) {
        state->has_latest_seq = true;
        state->active = true;
        state->latest_seq = header->seq;
        state->chunk_count = chunk_count;
        state->received_mask = 0U;
        state->final_chunk_len = 0U;
    } else if (header->seq != state->latest_seq || !state->active) {
        return RC_VIDEO_UDP_FRAME_REJECTED;
    }

    if (state->chunk_count != chunk_count) {
        return RC_VIDEO_UDP_FRAME_REJECTED;
    }
    if (is_final && 0U != state->final_chunk_len &&
        state->final_chunk_len != payload_len) {
        return RC_VIDEO_UDP_FRAME_REJECTED;
    }

    memcpy(frame_buf + offset, payload, payload_len);
    state->received_mask |= UINT64_C(1) << chunk_index;
    if (is_final) {
        state->final_chunk_len = payload_len;
    }

    if (state->received_mask != expected_mask(chunk_count)) {
        return RC_VIDEO_UDP_FRAME_INCOMPLETE;
    }

    *out_frame_len = ((size_t)chunk_count - 1U) *
                     RC_VIDEO_UDP_CHUNK_DATA_MAX + state->final_chunk_len;
    *out_frame_seq = state->latest_seq;
    state->active = false;
    return RC_VIDEO_UDP_FRAME_COMPLETE;
}
