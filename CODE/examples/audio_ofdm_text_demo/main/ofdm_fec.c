#include "ofdm_fec.h"

#include <stdbool.h>
#include <string.h>

#include <correct.h>

#define OFDM_FEC_LFSR_BITS 15U
#define OFDM_FEC_LFSR_OUTPUT_SHIFT 14U
#define OFDM_FEC_LFSR_FEEDBACK_SHIFT_A 14U
#define OFDM_FEC_LFSR_FEEDBACK_SHIFT_B 13U
#define OFDM_FEC_LFSR_SEED_DEFAULT UINT16_C(1)
#define OFDM_FEC_SEED_SALT UINT32_C(0x6D2B79F5)
#define OFDM_FEC_SEED_MULTIPLIER UINT32_C(0x45D9F3B)
#define OFDM_FEC_SEED_FRAME_MIX UINT32_C(0x9E3779B9)
#define OFDM_FEC_RS_PARITY_SYMBOLS 16U
#define OFDM_FEC_SESSION_INVALID UINT16_C(0)

static correct_reed_solomon *s_reed_solomon = NULL;

static uint8_t lfsr_next_bit(uint16_t *state)
{
    const uint8_t output = (uint8_t)((*state >> OFDM_FEC_LFSR_OUTPUT_SHIFT) & 1U);
    const uint8_t feedback = (uint8_t)(
        (((*state >> OFDM_FEC_LFSR_FEEDBACK_SHIFT_A) ^
          (*state >> OFDM_FEC_LFSR_FEEDBACK_SHIFT_B)) & 1U));

    *state = (uint16_t)((*state << 1U) & OFDM_FEC_LFSR_MASK);
    *state = (uint16_t)(*state | feedback);
    return output;
}

static uint16_t normalize_seed(uint16_t seed)
{
    seed = (uint16_t)(seed & OFDM_FEC_LFSR_MASK);
    return 0U == seed ? OFDM_FEC_LFSR_SEED_DEFAULT : seed;
}

static ofdm_fec_result_t check_ready(const uint8_t *input,
                                     const uint8_t *output)
{
    if (NULL == input || NULL == output) {
        return OFDM_FEC_INVALID_ARGUMENT;
    }
    if (NULL == s_reed_solomon) {
        return OFDM_FEC_NOT_INITIALIZED;
    }
    return OFDM_FEC_OK;
}

ofdm_fec_result_t ofdm_fec_init(void)
{
    if (NULL != s_reed_solomon) {
        return OFDM_FEC_OK;
    }

    s_reed_solomon = correct_reed_solomon_create(
        correct_rs_primitive_polynomial_ccsds,
        1U,
        1U,
        OFDM_FEC_RS_PARITY_SYMBOLS);
    if (NULL == s_reed_solomon) {
        return OFDM_FEC_INIT_FAILED;
    }

    uint8_t warm_data[OFDM_RS_DATA_BYTES] = {0};
    uint8_t warm_encoded[OFDM_RS_CODEWORD_BYTES] = {0};
    uint8_t warm_decoded[OFDM_RS_DATA_BYTES] = {0};
    const ssize_t encoded_length = correct_reed_solomon_encode(
        s_reed_solomon, warm_data, sizeof(warm_data), warm_encoded);
    const ssize_t decoded_length = correct_reed_solomon_decode(
        s_reed_solomon, warm_encoded, sizeof(warm_encoded), warm_decoded);
    if (encoded_length < 0 ||
        (ssize_t)sizeof(warm_decoded) != decoded_length ||
        0 != memcmp(warm_data, warm_decoded, sizeof(warm_data))) {
        correct_reed_solomon_destroy(s_reed_solomon);
        s_reed_solomon = NULL;
        return OFDM_FEC_INIT_FAILED;
    }
    return OFDM_FEC_OK;
}

void ofdm_fec_deinit(void)
{
    if (NULL != s_reed_solomon) {
        correct_reed_solomon_destroy(s_reed_solomon);
        s_reed_solomon = NULL;
    }
}

uint16_t ofdm_fec_seed(uint16_t session_id, uint8_t frame_index)
{
    uint32_t mixed = OFDM_FEC_SEED_SALT ^ (uint32_t)session_id;

    mixed *= OFDM_FEC_SEED_MULTIPLIER;
    mixed ^= ((uint32_t)frame_index + 1U) * OFDM_FEC_SEED_FRAME_MIX;
    mixed ^= mixed >> 16U;
    mixed *= OFDM_FEC_SEED_MULTIPLIER;
    mixed ^= mixed >> 16U;

    return normalize_seed((uint16_t)mixed);
}

void ofdm_fec_scramble(uint8_t *data, size_t length, uint16_t seed)
{
    if (NULL == data && 0U != length) {
        return;
    }

    uint16_t state = normalize_seed(seed);
    for (size_t byte_index = 0U; byte_index < length; ++byte_index) {
        for (uint8_t bit_index = 0U; bit_index < 8U; ++bit_index) {
            const uint8_t mask = (uint8_t)(UINT8_C(0x80) >> bit_index);
            if (0U != lfsr_next_bit(&state)) {
                data[byte_index] ^= mask;
            }
        }
    }
}

void ofdm_fec_descramble(uint8_t *data, size_t length, uint16_t seed)
{
    ofdm_fec_scramble(data, length, seed);
}

ofdm_fec_result_t ofdm_fec_encode(
    const uint8_t data[OFDM_FEC_DATA_BYTES],
    uint8_t encoded[OFDM_CODED_PAYLOAD_BYTES],
    uint16_t session_id,
    uint8_t frame_index)
{
    const ofdm_fec_result_t ready = check_ready(data, encoded);
    if (OFDM_FEC_OK != ready) {
        return ready;
    }

    uint8_t codewords[OFDM_RS_CODEWORD_COUNT][OFDM_RS_CODEWORD_BYTES] = {0};
    memset(encoded, 0, OFDM_CODED_PAYLOAD_BYTES);
    for (uint8_t row = 0U; row < OFDM_RS_CODEWORD_COUNT; ++row) {
        const ssize_t encoded_length = correct_reed_solomon_encode(
            s_reed_solomon,
            &data[(size_t)row * OFDM_RS_DATA_BYTES],
            OFDM_RS_DATA_BYTES,
            codewords[row]);
        if (encoded_length < 0) {
            return OFDM_FEC_ENCODE_FAILED;
        }
    }

    for (uint8_t column = 0U; column < OFDM_RS_CODEWORD_BYTES; ++column) {
        for (uint8_t row = 0U; row < OFDM_RS_CODEWORD_COUNT; ++row) {
            encoded[(size_t)column * OFDM_RS_CODEWORD_COUNT + row] =
                codewords[row][column];
        }
    }
    ofdm_fec_scramble(encoded, OFDM_CODED_PAYLOAD_BYTES,
                      ofdm_fec_seed(session_id, frame_index));
    return OFDM_FEC_OK;
}

ofdm_fec_result_t ofdm_fec_decode(
    const uint8_t encoded[OFDM_CODED_PAYLOAD_BYTES],
    uint8_t data[OFDM_FEC_DATA_BYTES],
    uint16_t session_id,
    uint8_t frame_index,
    uint16_t *corrected_symbols)
{
    const ofdm_fec_result_t ready = check_ready(encoded, data);
    if (OFDM_FEC_OK != ready) {
        return ready;
    }

    uint8_t descrambled[OFDM_CODED_PAYLOAD_BYTES] = {0};
    uint8_t codeword[OFDM_RS_CODEWORD_BYTES] = {0};
    uint8_t decoded_row[OFDM_RS_DATA_BYTES] = {0};
    uint8_t corrected_codeword[OFDM_RS_CODEWORD_BYTES] = {0};
    uint8_t decoded_data[OFDM_FEC_DATA_BYTES] = {0};
    uint16_t fixed_symbols = 0U;

    memcpy(descrambled, encoded, sizeof(descrambled));
    ofdm_fec_descramble(descrambled, sizeof(descrambled),
                        ofdm_fec_seed(session_id, frame_index));

    for (uint8_t row = 0U; row < OFDM_RS_CODEWORD_COUNT; ++row) {
        for (uint8_t column = 0U; column < OFDM_RS_CODEWORD_BYTES; ++column) {
            codeword[column] = descrambled[(size_t)column *
                                           OFDM_RS_CODEWORD_COUNT + row];
        }

        const ssize_t decoded_length = correct_reed_solomon_decode(
            s_reed_solomon, codeword, sizeof(codeword), decoded_row);
        if ((ssize_t)sizeof(decoded_row) != decoded_length) {
            memset(data, 0, OFDM_FEC_DATA_BYTES);
            return OFDM_FEC_DECODE_FAILED;
        }

        const ssize_t corrected_length = correct_reed_solomon_encode(
            s_reed_solomon, decoded_row, sizeof(decoded_row),
            corrected_codeword);
        if (corrected_length < 0) {
            memset(data, 0, OFDM_FEC_DATA_BYTES);
            return OFDM_FEC_DECODE_FAILED;
        }
        for (uint8_t column = 0U; column < OFDM_RS_CODEWORD_BYTES; ++column) {
            if (codeword[column] != corrected_codeword[column]) {
                ++fixed_symbols;
            }
        }
        memcpy(&decoded_data[(size_t)row * OFDM_RS_DATA_BYTES], decoded_row,
               OFDM_RS_DATA_BYTES);
    }

    memcpy(data, decoded_data, sizeof(decoded_data));
    if (NULL != corrected_symbols) {
        *corrected_symbols = fixed_symbols;
    }
    return OFDM_FEC_OK;
}
