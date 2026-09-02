#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "rc_tank_common.h"
#include "rc_video_udp_transport.h"

#define TEST_ASSERT(condition) do { if (!(condition)) return __LINE__; } while (0)

static int test_chunk_plan_stays_below_mtu(void)
{
    TEST_ASSERT(rc_video_udp_chunk_count(0) == 0);
    TEST_ASSERT(rc_video_udp_chunk_count(1200) == 1);
    TEST_ASSERT(rc_video_udp_chunk_count(2500) == 3);
    TEST_ASSERT(rc_video_udp_chunk_payload_len(2500, 0) == 1200);
    TEST_ASSERT(rc_video_udp_chunk_payload_len(2500, 1) == 1200);
    TEST_ASSERT(rc_video_udp_chunk_payload_len(2500, 2) == 100);
    TEST_ASSERT(sizeof(rc_video_header_t) + RC_VIDEO_UDP_CHUNK_DATA_MAX < 1500);
    return 0;
}

static int test_chunk_metadata_round_trip(void)
{
    const uint16_t reserved = rc_video_udp_pack_chunk_meta(17, 42);
    uint8_t index = 0;
    uint8_t count = 0;
    TEST_ASSERT(rc_video_udp_unpack_chunk_meta(reserved, &index, &count));
    TEST_ASSERT(index == 17);
    TEST_ASSERT(count == 42);
    TEST_ASSERT(!rc_video_udp_unpack_chunk_meta(0, &index, &count));
    return 0;
}

static int test_out_of_order_reassembly(void)
{
    static uint8_t source[2500];
    static uint8_t output[2500];
    rc_video_udp_reassembly_t state = {0};
    size_t output_len = 0;
    uint16_t output_seq = UINT16_MAX;

    for (size_t i = 0; i < sizeof(source); ++i) {
        source[i] = (uint8_t)(i * 31U + 7U);
    }

    const uint8_t order[] = {1, 0, 2};
    for (size_t i = 0; i < sizeof(order); ++i) {
        const uint8_t index = order[i];
        const size_t offset = (size_t)index * RC_VIDEO_UDP_CHUNK_DATA_MAX;
        const size_t payload_len = rc_video_udp_chunk_payload_len(sizeof(source), index);
        const rc_video_header_t header = {
            .magic = RC_VIDEO_MAGIC,
            .length = (uint16_t)payload_len,
            .seq = 77,
            .reserved = rc_video_udp_pack_chunk_meta(index, 3),
        };
        const rc_video_udp_push_result_t result = rc_video_udp_reassembly_push(
            &state, &header, source + offset, payload_len,
            output, sizeof(output), &output_len, &output_seq);
        TEST_ASSERT(result == (i + 1 == sizeof(order)
            ? RC_VIDEO_UDP_FRAME_COMPLETE : RC_VIDEO_UDP_FRAME_INCOMPLETE));
        if (i + 1U < sizeof(order)) {
            TEST_ASSERT(output_seq == UINT16_MAX);
        }
    }

    TEST_ASSERT(output_len == sizeof(source));
    TEST_ASSERT(output_seq == 77U);
    for (size_t i = 0; i < sizeof(source); ++i) {
        TEST_ASSERT(output[i] == source[i]);
    }
    return 0;
}

static int test_missing_old_frame_is_replaced_by_new_frame(void)
{
    static const uint8_t payload[RC_VIDEO_UDP_CHUNK_DATA_MAX] = {1, 2, 3, 4};
    static uint8_t output[2000];
    rc_video_udp_reassembly_t state = {0};
    size_t output_len = 0;
    uint16_t output_seq = UINT16_MAX;

    rc_video_header_t header = {
        .magic = RC_VIDEO_MAGIC,
        .length = sizeof(payload),
        .seq = 10,
        .reserved = rc_video_udp_pack_chunk_meta(0, 2),
    };
    TEST_ASSERT(rc_video_udp_reassembly_push(
        &state, &header, payload, sizeof(payload), output, sizeof(output),
        &output_len, &output_seq)
        == RC_VIDEO_UDP_FRAME_INCOMPLETE);
    TEST_ASSERT(output_seq == UINT16_MAX);

    header.seq = 11;
    header.reserved = rc_video_udp_pack_chunk_meta(0, 1);
    TEST_ASSERT(rc_video_udp_reassembly_push(
        &state, &header, payload, sizeof(payload), output, sizeof(output),
        &output_len, &output_seq)
        == RC_VIDEO_UDP_FRAME_COMPLETE);
    TEST_ASSERT(output_len == sizeof(payload));
    TEST_ASSERT(output_seq == 11U);
    return 0;
}

int main(void)
{
    int result = 0;
    if ((result = test_chunk_plan_stays_below_mtu()) != 0) return result;
    if ((result = test_chunk_metadata_round_trip()) != 0) return result;
    if ((result = test_out_of_order_reassembly()) != 0) return result;
    if ((result = test_missing_old_frame_is_replaced_by_new_frame()) != 0) return result;
    return 0;
}
