#include "ofdm_crc.h"
#include "ofdm_fec.h"

#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void fill_incrementing(uint8_t *data, size_t length)
{
    assert(NULL != data);
    for (size_t index = 0U; index < length; ++index) {
        data[index] = (uint8_t)(index * 37U + 11U);
    }
}

static void test_seed_and_scrambler(void)
{
    static const uint16_t sessions[] = {1U, 0x1234U, 0xFFFFU};
    static const uint8_t frame_indices[] = {
        0U, 1U, 2U, 3U, 4U, 5U, 6U, 7U, 8U, 9U};
    uint8_t original[OFDM_CODED_PAYLOAD_BYTES] = {0};
    uint8_t scrambled[OFDM_CODED_PAYLOAD_BYTES] = {0};

    for (uint8_t pattern = 0U; pattern < 3U; ++pattern) {
        if (0U == pattern) {
            memset(original, 0, sizeof(original));
        } else if (1U == pattern) {
            memset(original, UINT8_MAX, sizeof(original));
        } else {
            fill_incrementing(original, sizeof(original));
        }
        for (size_t session_index = 0U;
             session_index < sizeof(sessions) / sizeof(sessions[0]);
             ++session_index) {
            for (size_t frame_index = 0U;
                 frame_index < sizeof(frame_indices) / sizeof(frame_indices[0]);
                 ++frame_index) {
                const uint16_t seed = ofdm_fec_seed(
                    sessions[session_index], frame_indices[frame_index]);
                assert(0U != seed);
                assert(OFDM_FEC_LFSR_MASK >= seed);
                memcpy(scrambled, original, sizeof(scrambled));
                ofdm_fec_scramble(scrambled, sizeof(scrambled), seed);
                assert(0 != memcmp(original, scrambled, sizeof(original)));
                ofdm_fec_descramble(scrambled, sizeof(scrambled), seed);
                assert(0 == memcmp(original, scrambled, sizeof(original)));
            }
        }
    }
}

static void test_fec_round_trip(void)
{
    uint8_t original[OFDM_FEC_DATA_BYTES] = {0};
    uint8_t encoded[OFDM_CODED_PAYLOAD_BYTES] = {0};
    uint8_t decoded[OFDM_FEC_DATA_BYTES] = {0};
    uint16_t corrected_symbols = 0U;

    fill_incrementing(original, sizeof(original));
    assert(OFDM_FEC_OK == ofdm_fec_init());
    assert(OFDM_FEC_OK == ofdm_fec_encode(
                              original, encoded, 0x1234U, 2U));
    assert(UINT16_C(0x7D82) == ofdm_fec_seed(0x1234U, 2U));
    assert(UINT32_C(0xF2E8E349) ==
           ofdm_crc32_iso_hdlc(encoded, sizeof(encoded)));
    assert(UINT8_C(0xF0) == encoded[0]);
    assert(UINT8_C(0x5E) == encoded[sizeof(encoded) - 1U]);
    assert(OFDM_FEC_OK == ofdm_fec_decode(
                              encoded, decoded, 0x1234U, 2U,
                              &corrected_symbols));
    assert(0U == corrected_symbols);
    assert(0 == memcmp(original, decoded, sizeof(original)));
}

static void test_zero_to_eight_errors_per_codeword(void)
{
    uint8_t original[OFDM_FEC_DATA_BYTES] = {0};
    uint8_t encoded[OFDM_CODED_PAYLOAD_BYTES] = {0};
    uint8_t damaged[OFDM_CODED_PAYLOAD_BYTES] = {0};
    uint8_t decoded[OFDM_FEC_DATA_BYTES] = {0};
    uint16_t corrected_symbols = 0U;

    fill_incrementing(original, sizeof(original));
    assert(OFDM_FEC_OK == ofdm_fec_encode(
                              original, encoded, 0x2345U, 3U));
    for (uint8_t error_count = 0U; error_count <= 8U; ++error_count) {
        for (uint8_t row = 0U; row < OFDM_RS_CODEWORD_COUNT; ++row) {
            memcpy(damaged, encoded, sizeof(damaged));
            for (uint8_t column = 0U; column < error_count; ++column) {
                const size_t offset =
                    (size_t)column * OFDM_RS_CODEWORD_COUNT + row;
                damaged[offset] ^= (uint8_t)(0x31U + column * 7U + row);
            }
            assert(OFDM_FEC_OK == ofdm_fec_decode(
                                      damaged, decoded, 0x2345U, 3U,
                                      &corrected_symbols));
            assert(error_count == corrected_symbols);
            assert(0 == memcmp(original, decoded, sizeof(original)));
        }
    }
}

static void test_crc_gate_for_unrecoverable_errors(void)
{
    uint8_t original[OFDM_FEC_DATA_BYTES] = {0};
    uint8_t encoded[OFDM_CODED_PAYLOAD_BYTES] = {0};
    uint8_t damaged[OFDM_CODED_PAYLOAD_BYTES] = {0};
    uint8_t decoded[OFDM_FEC_DATA_BYTES] = {0};
    uint16_t corrected_symbols = 0U;

    fill_incrementing(original, sizeof(original));
    const uint32_t original_crc = ofdm_crc32_iso_hdlc(
        original, sizeof(original));
    assert(OFDM_FEC_OK == ofdm_fec_encode(
                              original, encoded, 0x3456U, 4U));
    memcpy(damaged, encoded, sizeof(damaged));
    for (uint8_t column = 0U; column < 9U; ++column) {
        damaged[(size_t)column * OFDM_RS_CODEWORD_COUNT] ^=
            (uint8_t)(0xA1U + column * 13U);
    }

    const ofdm_fec_result_t result = ofdm_fec_decode(
        damaged, decoded, 0x3456U, 4U, &corrected_symbols);
    assert(OFDM_FEC_OK != result ||
           original_crc != ofdm_crc32_iso_hdlc(decoded, sizeof(decoded)));
}

int main(void)
{
    test_seed_and_scrambler();
    test_fec_round_trip();
    test_zero_to_eight_errors_per_codeword();
    test_crc_gate_for_unrecoverable_errors();
    ofdm_fec_deinit();
    puts("ofdm_fec_test: PASS");
    return 0;
}
