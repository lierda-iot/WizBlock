#ifndef OFDM_FEC_H
#define OFDM_FEC_H

#include <stddef.h>
#include <stdint.h>

#define OFDM_RS_DATA_BYTES 16U
#define OFDM_RS_CODEWORD_BYTES 32U
#define OFDM_RS_CODEWORD_COUNT 7U
#define OFDM_FEC_DATA_BYTES \
    (OFDM_RS_DATA_BYTES * OFDM_RS_CODEWORD_COUNT)
#define OFDM_CODED_PAYLOAD_BYTES \
    (OFDM_RS_CODEWORD_BYTES * OFDM_RS_CODEWORD_COUNT)
#define OFDM_FEC_LFSR_MASK UINT16_C(0x7FFF)

typedef enum {
    OFDM_FEC_OK = 0,
    OFDM_FEC_INVALID_ARGUMENT,
    OFDM_FEC_NOT_INITIALIZED,
    OFDM_FEC_INIT_FAILED,
    OFDM_FEC_ENCODE_FAILED,
    OFDM_FEC_DECODE_FAILED,
} ofdm_fec_result_t;

ofdm_fec_result_t ofdm_fec_init(void);
void ofdm_fec_deinit(void);

uint16_t ofdm_fec_seed(uint16_t session_id, uint8_t frame_index);
void ofdm_fec_scramble(uint8_t *data, size_t length, uint16_t seed);
void ofdm_fec_descramble(uint8_t *data, size_t length, uint16_t seed);

ofdm_fec_result_t ofdm_fec_encode(
    const uint8_t data[OFDM_FEC_DATA_BYTES],
    uint8_t encoded[OFDM_CODED_PAYLOAD_BYTES],
    uint16_t session_id,
    uint8_t frame_index);
ofdm_fec_result_t ofdm_fec_decode(
    const uint8_t encoded[OFDM_CODED_PAYLOAD_BYTES],
    uint8_t data[OFDM_FEC_DATA_BYTES],
    uint16_t session_id,
    uint8_t frame_index,
    uint16_t *corrected_symbols);

#endif
