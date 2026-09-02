#ifndef OFDM_PHY_H
#define OFDM_PHY_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ofdm_fec.h"
#include "ofdm_frame.h"
#include "ofdm_profile.h"

#define OFDM_SAMPLE_RATE_HZ 48000U
#define OFDM_FFT_SIZE 256U
#define OFDM_CP_SAMPLES 256U
#define OFDM_SYMBOL_SAMPLES (OFDM_FFT_SIZE + OFDM_CP_SAMPLES)
#define OFDM_FIRST_CARRIER_BIN OFDM_NORMAL_FIRST_CARRIER_BIN
#define OFDM_LAST_CARRIER_BIN OFDM_NORMAL_LAST_CARRIER_BIN
#define OFDM_PILOT_COUNT OFDM_NORMAL_PILOT_COUNT
#define OFDM_DATA_CARRIER_COUNT OFDM_NORMAL_DATA_CARRIER_COUNT
#define OFDM_PAYLOAD_BITS (OFDM_CODED_PAYLOAD_BYTES * 8U)
#define OFDM_PAYLOAD_QPSK_SYMBOLS (OFDM_PAYLOAD_BITS / 2U)
#define OFDM_PAYLOAD_SYMBOL_COUNT \
    ((OFDM_PAYLOAD_QPSK_SYMBOLS + OFDM_DATA_CARRIER_COUNT - 1U) / \
     OFDM_DATA_CARRIER_COUNT)
#define OFDM_PAYLOAD_SAMPLE_COUNT \
    (OFDM_PAYLOAD_SYMBOL_COUNT * OFDM_SYMBOL_SAMPLES)
#define OFDM_HEADER_REPEAT 3U
#define OFDM_HEADER_BITS (OFDM_FRAME_HEADER_BYTES * 8U)
#define OFDM_HEADER_CODED_BITS (OFDM_HEADER_BITS * OFDM_HEADER_REPEAT)
#define OFDM_HEADER_SYMBOL_COUNT \
    ((OFDM_HEADER_CODED_BITS + OFDM_DATA_CARRIER_COUNT - 1U) / \
     OFDM_DATA_CARRIER_COUNT)
#define OFDM_CHIRP_SAMPLES 960U
#define OFDM_GUARD_SAMPLES 240U
#define OFDM_LTS_SYMBOL_COUNT 3U
#define OFDM_PHY_RESPONSE_GROUP_COUNT 8U
#define OFDM_PHY_RESPONSE_INVALID_DB (-120.0F)
#define OFDM_PHY_SC_MIN_SCORE 0.75F
#define OFDM_PHY_LTS_MIN_SCORE 0.75F
#define OFDM_FRAME_CHIRP_OFFSET OFDM_GUARD_SAMPLES
#define OFDM_FRAME_SC_OFFSET \
    (OFDM_FRAME_CHIRP_OFFSET + OFDM_CHIRP_SAMPLES)
#define OFDM_FRAME_LTS_OFFSET \
    (OFDM_FRAME_SC_OFFSET + OFDM_SYMBOL_SAMPLES)
#define OFDM_FRAME_HEADER_OFFSET \
    (OFDM_FRAME_LTS_OFFSET + \
     OFDM_LTS_SYMBOL_COUNT * OFDM_SYMBOL_SAMPLES)
#define OFDM_FRAME_PAYLOAD_OFFSET \
    (OFDM_FRAME_HEADER_OFFSET + \
     OFDM_HEADER_SYMBOL_COUNT * OFDM_SYMBOL_SAMPLES)
#define OFDM_FRAME_SAMPLE_COUNT \
    (OFDM_FRAME_PAYLOAD_OFFSET + OFDM_PAYLOAD_SAMPLE_COUNT + \
     OFDM_GUARD_SAMPLES)

typedef enum {
    OFDM_PHY_OK = 0,
    OFDM_PHY_INVALID_ARGUMENT,
    OFDM_PHY_NOT_INITIALIZED,
    OFDM_PHY_INIT_FAILED,
    OFDM_PHY_INVALID_SIGNAL,
} ofdm_phy_result_t;

typedef struct {
    float sc_score;
    float lts_score;
    float header_evm_db;
    float payload_evm_db;
    float channel_min_db;
    float channel_max_db;
    float channel_spread_db;
    uint8_t channel_usable_carriers;
    float channel_group_db[OFDM_PHY_RESPONSE_GROUP_COUNT];
} ofdm_phy_frame_metrics_t;

ofdm_phy_result_t ofdm_phy_init(void);
void ofdm_phy_deinit(void);
void ofdm_phy_fill_chirp(float samples[OFDM_CHIRP_SAMPLES]);

bool ofdm_phy_is_pilot_bin(uint16_t bin);
uint16_t ofdm_phy_get_data_bin(size_t data_index);
bool ofdm_phy_get_response_group_range(size_t group_index,
                                       uint16_t *first_bin,
                                       uint16_t *last_bin);

ofdm_phy_result_t ofdm_phy_modulate_payload(
    const uint8_t payload[OFDM_CODED_PAYLOAD_BYTES],
    float samples[OFDM_PAYLOAD_SAMPLE_COUNT]);
ofdm_phy_result_t ofdm_phy_demodulate_payload(
    const float samples[OFDM_PAYLOAD_SAMPLE_COUNT],
    uint8_t payload[OFDM_CODED_PAYLOAD_BYTES],
    float *evm_db);

bool ofdm_phy_measure_training(
    const float samples[OFDM_FRAME_SAMPLE_COUNT],
    float *sc_score,
    float *lts_score);

ofdm_phy_result_t ofdm_phy_modulate_frame(
    const uint8_t header_wire[OFDM_FRAME_HEADER_BYTES],
    const uint8_t payload[OFDM_CODED_PAYLOAD_BYTES],
    float samples[OFDM_FRAME_SAMPLE_COUNT]);
ofdm_phy_result_t ofdm_phy_demodulate_frame(
    const float samples[OFDM_FRAME_SAMPLE_COUNT],
    uint8_t header_wire[OFDM_FRAME_HEADER_BYTES],
    uint8_t payload[OFDM_CODED_PAYLOAD_BYTES],
    ofdm_phy_frame_metrics_t *metrics);

#endif
