#ifndef OFDM_CALIBRATION_H
#define OFDM_CALIBRATION_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "ofdm_phy.h"
#include "ofdm_profile.h"

#define OFDM_CAL_MAGIC UINT32_C(0x4F43414C)
#define OFDM_CAL_VERSION 1U
#define OFDM_CAL_PAYLOAD_BYTES 24U
#define OFDM_CAL_TX_PROFILE_COUNT 4U
#define OFDM_CAL_RX_GAIN_COUNT 4U
#define OFDM_CAL_SAMPLE_REPEAT_COUNT 2U
#define OFDM_CAL_START_TRANSMIT_COUNT 4U
#define OFDM_CAL_CONTROL_TRANSMIT_COUNT 2U
#define OFDM_CAL_END_TRANSMIT_COUNT 4U
#define OFDM_CAL_TIMEOUT_MS 130000U
#define OFDM_CAL_FRAMES_PER_COMBINATION \
    (1U + OFDM_CAL_SAMPLE_REPEAT_COUNT)
#define OFDM_CAL_TOTAL_FRAME_COUNT \
    (2U + OFDM_CAL_TX_PROFILE_COUNT * OFDM_CAL_RX_GAIN_COUNT * \
         OFDM_CAL_FRAMES_PER_COMBINATION)
#define OFDM_CAL_SAFE_TX_PROFILE_INDEX 3U
#define OFDM_CAL_DEFAULT_RX_GAIN_INDEX OFDM_NORMAL_RX_GAIN_INDEX
#define OFDM_CAL_BAND_GROUP_COUNT OFDM_PHY_RESPONSE_GROUP_COUNT
#define OFDM_CAL_RECOMMEND_BAND_MARGIN_DB 12.0F

typedef enum {
    OFDM_CAL_FRAME_START = 1,
    OFDM_CAL_FRAME_CONTROL = 2,
    OFDM_CAL_FRAME_SAMPLE = 3,
    OFDM_CAL_FRAME_END = 4,
} ofdm_calibration_frame_kind_t;

typedef struct {
    uint8_t volume_percent;
    uint16_t pcm_scale;
} ofdm_calibration_tx_profile_t;

typedef struct {
    uint8_t kind;
    uint8_t tx_profile;
    uint8_t rx_gain_index;
    uint8_t repeat_index;
    uint8_t repeat_count;
    uint8_t volume_percent;
    uint16_t pcm_scale;
    uint16_t sequence;
    uint16_t total_frames;
    uint16_t first_bin;
    uint16_t last_bin;
    uint16_t run_id;
} ofdm_calibration_frame_t;

typedef struct {
    uint32_t sample_count;
    uint32_t phy_ok_count;
    uint32_t crc_ok_count;
    uint32_t clip_samples;
    uint32_t peak_max;
    uint32_t evm_count;
    float header_evm_sum_db;
    float payload_evm_sum_db;
    float band_db_sum[OFDM_CAL_BAND_GROUP_COUNT];
    uint32_t band_observation_count[OFDM_CAL_BAND_GROUP_COUNT];
} ofdm_calibration_cell_t;

typedef struct {
    ofdm_calibration_cell_t cells[OFDM_CAL_TX_PROFILE_COUNT]
                                [OFDM_CAL_RX_GAIN_COUNT];
} ofdm_calibration_stats_t;

typedef struct {
    bool valid;
    uint8_t tx_profile;
    uint8_t rx_gain_index;
    uint16_t first_bin;
    uint16_t last_bin;
    float crc_success_rate;
    float phy_success_rate;
    float payload_evm_db;
    float band_spread_db;
    float band_db[OFDM_CAL_BAND_GROUP_COUNT];
} ofdm_calibration_recommendation_t;

const ofdm_calibration_tx_profile_t *ofdm_calibration_get_tx_profile(
    size_t profile_index);
bool ofdm_calibration_get_rx_gain(size_t gain_index, float *gain_db);

bool ofdm_calibration_encode(
    const ofdm_calibration_frame_t *frame,
    uint8_t payload[OFDM_CAL_PAYLOAD_BYTES]);
bool ofdm_calibration_decode(
    const uint8_t payload[OFDM_CAL_PAYLOAD_BYTES],
    ofdm_calibration_frame_t *frame);
bool ofdm_calibration_prepare_frame(
    uint16_t run_id,
    uint16_t sequence,
    ofdm_calibration_frame_t *frame);
bool ofdm_calibration_is_end_sequence(uint16_t sequence);
uint8_t ofdm_calibration_transmit_count(uint16_t sequence);
bool ofdm_calibration_is_retransmittable_sequence(uint16_t sequence);
bool ofdm_calibration_is_header_recoverable_sequence(uint16_t sequence);
bool ofdm_calibration_find_sequence(uint16_t session_id,
                                    uint16_t *sequence);
bool ofdm_calibration_can_recover_start(
    const ofdm_calibration_frame_t *frame);
bool ofdm_calibration_session_matches(
    uint16_t session_id,
    const ofdm_calibration_frame_t *frame);

uint16_t ofdm_calibration_make_session_id(
    uint8_t kind,
    uint8_t tx_profile,
    uint8_t rx_gain_index,
    uint8_t repeat_index,
    uint16_t sequence);
bool ofdm_calibration_parse_session_id(
    uint16_t session_id,
    uint8_t *kind,
    uint8_t *tx_profile,
    uint8_t *rx_gain_index,
    uint8_t *repeat_index);

void ofdm_calibration_reset_stats(ofdm_calibration_stats_t *stats);
void ofdm_calibration_record_sample(
    ofdm_calibration_stats_t *stats,
    uint8_t tx_profile,
    uint8_t rx_gain_index,
    bool phy_ok,
    bool crc_ok,
    uint32_t peak,
    uint32_t clip_samples,
    const ofdm_phy_frame_metrics_t *metrics);
bool ofdm_calibration_select_recommendation(
    const ofdm_calibration_stats_t *stats,
    ofdm_calibration_recommendation_t *recommendation);

float ofdm_calibration_cell_crc_rate(
    const ofdm_calibration_cell_t *cell);
float ofdm_calibration_cell_phy_rate(
    const ofdm_calibration_cell_t *cell);
float ofdm_calibration_cell_payload_evm_db(
    const ofdm_calibration_cell_t *cell);
float ofdm_calibration_cell_band_spread_db(
    const ofdm_calibration_cell_t *cell);
float ofdm_calibration_cell_band_db(
    const ofdm_calibration_cell_t *cell,
    size_t group_index);

#endif
