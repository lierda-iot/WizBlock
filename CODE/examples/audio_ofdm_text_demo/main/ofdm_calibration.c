#include "ofdm_calibration.h"

#include <math.h>
#include <string.h>

#define OFDM_CAL_SESSION_PREFIX UINT16_C(0xA000)
#define OFDM_CAL_SESSION_PREFIX_MASK UINT16_C(0xE000)
#define OFDM_CAL_SESSION_KIND_SHIFT 10U
#define OFDM_CAL_SESSION_TX_SHIFT 8U
#define OFDM_CAL_SESSION_RX_SHIFT 6U
#define OFDM_CAL_SESSION_REPEAT_SHIFT 4U
#define OFDM_CAL_SESSION_KIND_MASK UINT16_C(0x1C00)
#define OFDM_CAL_SESSION_TX_MASK UINT16_C(0x0300)
#define OFDM_CAL_SESSION_RX_MASK UINT16_C(0x00C0)
#define OFDM_CAL_SESSION_REPEAT_MASK UINT16_C(0x0030)
#define OFDM_CAL_SESSION_SEQUENCE_MASK UINT16_C(0x000F)
#define OFDM_CAL_INVALID_DB (-120.0F)

static const ofdm_calibration_tx_profile_t s_tx_profiles[
    OFDM_CAL_TX_PROFILE_COUNT] = {
        {70U, 14000U},
        {80U, 20000U},
        {OFDM_NORMAL_OUTPUT_VOLUME_PERCENT, OFDM_NORMAL_PCM_SCALE},
        {90U, 28000U},
    };

static const float s_rx_gains[OFDM_CAL_RX_GAIN_COUNT] = {
    24.0F,
    OFDM_NORMAL_INPUT_GAIN_DB,
    34.5F,
    37.5F,
};

static void write_u16_be(uint8_t *output, uint16_t value)
{
    output[0] = (uint8_t)(value >> 8U);
    output[1] = (uint8_t)value;
}

static uint16_t read_u16_be(const uint8_t *input)
{
    return (uint16_t)(((uint16_t)input[0] << 8U) | input[1]);
}

static bool valid_kind(uint8_t kind)
{
    return OFDM_CAL_FRAME_START == kind ||
           OFDM_CAL_FRAME_CONTROL == kind ||
           OFDM_CAL_FRAME_SAMPLE == kind ||
           OFDM_CAL_FRAME_END == kind;
}

static bool get_frame_layout(uint16_t sequence,
                             uint8_t *kind,
                             uint8_t *tx_profile,
                             uint8_t *rx_gain_index,
                             uint8_t *repeat_index,
                             uint8_t *repeat_count)
{
    if (NULL == kind || NULL == tx_profile || NULL == rx_gain_index ||
        NULL == repeat_index || NULL == repeat_count ||
        OFDM_CAL_TOTAL_FRAME_COUNT <= sequence) {
        return false;
    }

    *repeat_index = 0U;
    *repeat_count = 0U;
    if (0U == sequence) {
        *kind = OFDM_CAL_FRAME_START;
        *tx_profile = OFDM_CAL_SAFE_TX_PROFILE_INDEX;
        *rx_gain_index = OFDM_CAL_DEFAULT_RX_GAIN_INDEX;
        return true;
    }
    if (OFDM_CAL_TOTAL_FRAME_COUNT - 1U == sequence) {
        *kind = OFDM_CAL_FRAME_END;
        *tx_profile = OFDM_CAL_SAFE_TX_PROFILE_INDEX;
        *rx_gain_index = OFDM_CAL_DEFAULT_RX_GAIN_INDEX;
        return true;
    }

    const uint16_t combination_sequence = (uint16_t)(sequence - 1U);
    const uint8_t slot = (uint8_t)(combination_sequence %
                                   OFDM_CAL_FRAMES_PER_COMBINATION);
    const uint8_t combination = (uint8_t)(combination_sequence /
                                          OFDM_CAL_FRAMES_PER_COMBINATION);
    *tx_profile = (uint8_t)(combination / OFDM_CAL_RX_GAIN_COUNT);
    *rx_gain_index = (uint8_t)(combination % OFDM_CAL_RX_GAIN_COUNT);
    if (0U == slot) {
        *kind = OFDM_CAL_FRAME_CONTROL;
    } else {
        *kind = OFDM_CAL_FRAME_SAMPLE;
        *repeat_index = (uint8_t)(slot - 1U);
        *repeat_count = OFDM_CAL_SAMPLE_REPEAT_COUNT;
    }
    return true;
}

static bool valid_frame_values(const ofdm_calibration_frame_t *frame)
{
    if (NULL == frame || !valid_kind(frame->kind) ||
        OFDM_CAL_TX_PROFILE_COUNT <= frame->tx_profile ||
        OFDM_CAL_RX_GAIN_COUNT <= frame->rx_gain_index ||
        0U == frame->volume_percent ||
        100U < frame->volume_percent || 0U == frame->pcm_scale ||
        0U == frame->run_id ||
        OFDM_CAL_TOTAL_FRAME_COUNT != frame->total_frames ||
        frame->sequence >= frame->total_frames ||
        OFDM_FIRST_CARRIER_BIN != frame->first_bin ||
        OFDM_LAST_CARRIER_BIN != frame->last_bin) {
        return false;
    }
    uint8_t expected_kind = 0U;
    uint8_t expected_tx = 0U;
    uint8_t expected_rx = 0U;
    uint8_t expected_repeat = 0U;
    uint8_t expected_repeat_count = 0U;
    if (!get_frame_layout(frame->sequence, &expected_kind, &expected_tx,
                          &expected_rx, &expected_repeat,
                          &expected_repeat_count) ||
        expected_kind != frame->kind ||
        expected_tx != frame->tx_profile ||
        expected_rx != frame->rx_gain_index ||
        expected_repeat != frame->repeat_index ||
        expected_repeat_count != frame->repeat_count) {
        return false;
    }
    const ofdm_calibration_tx_profile_t *tx_profile =
        ofdm_calibration_get_tx_profile(frame->tx_profile);
    if (NULL == tx_profile || tx_profile->volume_percent !=
                                  frame->volume_percent ||
        tx_profile->pcm_scale != frame->pcm_scale) {
        return false;
    }
    return true;
}

const ofdm_calibration_tx_profile_t *ofdm_calibration_get_tx_profile(
    size_t profile_index)
{
    return profile_index < OFDM_CAL_TX_PROFILE_COUNT
               ? &s_tx_profiles[profile_index]
               : NULL;
}

bool ofdm_calibration_get_rx_gain(size_t gain_index, float *gain_db)
{
    if (NULL == gain_db || OFDM_CAL_RX_GAIN_COUNT <= gain_index) {
        return false;
    }
    *gain_db = s_rx_gains[gain_index];
    return true;
}

bool ofdm_calibration_encode(
    const ofdm_calibration_frame_t *frame,
    uint8_t payload[OFDM_CAL_PAYLOAD_BYTES])
{
    if (NULL == payload || !valid_frame_values(frame)) {
        return false;
    }
    memset(payload, 0, OFDM_CAL_PAYLOAD_BYTES);
    write_u16_be(&payload[0], (uint16_t)(OFDM_CAL_MAGIC >> 16U));
    write_u16_be(&payload[2], (uint16_t)OFDM_CAL_MAGIC);
    payload[4] = OFDM_CAL_VERSION;
    payload[5] = frame->kind;
    payload[6] = frame->tx_profile;
    payload[7] = frame->rx_gain_index;
    payload[8] = frame->repeat_index;
    payload[9] = frame->repeat_count;
    payload[10] = frame->volume_percent;
    payload[11] = 0U;
    write_u16_be(&payload[12], frame->pcm_scale);
    write_u16_be(&payload[14], frame->sequence);
    write_u16_be(&payload[16], frame->total_frames);
    write_u16_be(&payload[18], frame->first_bin);
    write_u16_be(&payload[20], frame->last_bin);
    write_u16_be(&payload[22], frame->run_id);
    return true;
}

bool ofdm_calibration_decode(
    const uint8_t payload[OFDM_CAL_PAYLOAD_BYTES],
    ofdm_calibration_frame_t *frame)
{
    if (NULL == payload || NULL == frame) {
        return false;
    }
    const uint32_t magic = ((uint32_t)read_u16_be(&payload[0]) << 16U) |
                           read_u16_be(&payload[2]);
    if (OFDM_CAL_MAGIC != magic || OFDM_CAL_VERSION != payload[4] ||
        0U != payload[11]) {
        return false;
    }
    frame->kind = payload[5];
    frame->tx_profile = payload[6];
    frame->rx_gain_index = payload[7];
    frame->repeat_index = payload[8];
    frame->repeat_count = payload[9];
    frame->volume_percent = payload[10];
    frame->pcm_scale = read_u16_be(&payload[12]);
    frame->sequence = read_u16_be(&payload[14]);
    frame->total_frames = read_u16_be(&payload[16]);
    frame->first_bin = read_u16_be(&payload[18]);
    frame->last_bin = read_u16_be(&payload[20]);
    frame->run_id = read_u16_be(&payload[22]);
    return valid_frame_values(frame);
}

bool ofdm_calibration_prepare_frame(
    uint16_t run_id,
    uint16_t sequence,
    ofdm_calibration_frame_t *frame)
{
    if (NULL == frame || 0U == run_id ||
        OFDM_CAL_TOTAL_FRAME_COUNT <= sequence) {
        return false;
    }

    memset(frame, 0, sizeof(*frame));
    frame->run_id = run_id;
    frame->sequence = sequence;
    frame->total_frames = OFDM_CAL_TOTAL_FRAME_COUNT;
    frame->first_bin = OFDM_FIRST_CARRIER_BIN;
    frame->last_bin = OFDM_LAST_CARRIER_BIN;

    if (!get_frame_layout(sequence, &frame->kind, &frame->tx_profile,
                          &frame->rx_gain_index, &frame->repeat_index,
                          &frame->repeat_count)) {
        return false;
    }

    const ofdm_calibration_tx_profile_t *profile =
        ofdm_calibration_get_tx_profile(frame->tx_profile);
    if (NULL == profile) {
        return false;
    }
    frame->volume_percent = profile->volume_percent;
    frame->pcm_scale = profile->pcm_scale;
    return valid_frame_values(frame);
}

bool ofdm_calibration_is_end_sequence(uint16_t sequence)
{
    return OFDM_CAL_TOTAL_FRAME_COUNT - 1U == sequence;
}

uint8_t ofdm_calibration_transmit_count(uint16_t sequence)
{
    if (OFDM_CAL_TOTAL_FRAME_COUNT <= sequence) {
        return 0U;
    }
    uint8_t kind = 0U;
    uint8_t tx_profile = 0U;
    uint8_t rx_gain_index = 0U;
    uint8_t repeat_index = 0U;
    uint8_t repeat_count = 0U;
    if (!get_frame_layout(sequence, &kind, &tx_profile, &rx_gain_index,
                          &repeat_index, &repeat_count)) {
        return 0U;
    }
    if (OFDM_CAL_FRAME_END == kind) {
        return OFDM_CAL_END_TRANSMIT_COUNT;
    }
    if (OFDM_CAL_FRAME_START == kind) {
        return OFDM_CAL_START_TRANSMIT_COUNT;
    }
    if (OFDM_CAL_FRAME_CONTROL == kind) {
        return OFDM_CAL_CONTROL_TRANSMIT_COUNT;
    }
    return 1U;
}

bool ofdm_calibration_is_retransmittable_sequence(uint16_t sequence)
{
    uint8_t kind = 0U;
    uint8_t tx_profile = 0U;
    uint8_t rx_gain_index = 0U;
    uint8_t repeat_index = 0U;
    uint8_t repeat_count = 0U;
    if (!get_frame_layout(sequence, &kind, &tx_profile, &rx_gain_index,
                          &repeat_index, &repeat_count)) {
        return false;
    }
    return OFDM_CAL_FRAME_START == kind ||
           OFDM_CAL_FRAME_CONTROL == kind || OFDM_CAL_FRAME_END == kind;
}

bool ofdm_calibration_is_header_recoverable_sequence(uint16_t sequence)
{
    uint8_t kind = 0U;
    uint8_t tx_profile = 0U;
    uint8_t rx_gain_index = 0U;
    uint8_t repeat_index = 0U;
    uint8_t repeat_count = 0U;
    if (!get_frame_layout(sequence, &kind, &tx_profile, &rx_gain_index,
                          &repeat_index, &repeat_count)) {
        return false;
    }
    return OFDM_CAL_FRAME_CONTROL == kind || OFDM_CAL_FRAME_END == kind;
}

bool ofdm_calibration_can_recover_start(
    const ofdm_calibration_frame_t *frame)
{
    return valid_frame_values(frame) &&
           (OFDM_CAL_FRAME_CONTROL == frame->kind ||
            OFDM_CAL_FRAME_SAMPLE == frame->kind);
}

bool ofdm_calibration_session_matches(
    uint16_t session_id,
    const ofdm_calibration_frame_t *frame)
{
    if (NULL == frame || !valid_frame_values(frame)) {
        return false;
    }
    return session_id == ofdm_calibration_make_session_id(
                             frame->kind, frame->tx_profile,
                             frame->rx_gain_index, frame->repeat_index,
                             frame->sequence);
}

uint16_t ofdm_calibration_make_session_id(
    uint8_t kind,
    uint8_t tx_profile,
    uint8_t rx_gain_index,
    uint8_t repeat_index,
    uint16_t sequence)
{
    return (uint16_t)(OFDM_CAL_SESSION_PREFIX |
                      (((uint16_t)kind << OFDM_CAL_SESSION_KIND_SHIFT) &
                       OFDM_CAL_SESSION_KIND_MASK) |
                      (((uint16_t)tx_profile << OFDM_CAL_SESSION_TX_SHIFT) &
                       OFDM_CAL_SESSION_TX_MASK) |
                      (((uint16_t)rx_gain_index << OFDM_CAL_SESSION_RX_SHIFT) &
                       OFDM_CAL_SESSION_RX_MASK) |
                      (((uint16_t)repeat_index <<
                        OFDM_CAL_SESSION_REPEAT_SHIFT) &
                       OFDM_CAL_SESSION_REPEAT_MASK) |
                      (sequence & OFDM_CAL_SESSION_SEQUENCE_MASK));
}

bool ofdm_calibration_find_sequence(uint16_t session_id,
                                    uint16_t *sequence)
{
    if (NULL == sequence) {
        return false;
    }
    for (uint16_t candidate = 0U;
         OFDM_CAL_TOTAL_FRAME_COUNT > candidate; ++candidate) {
        ofdm_calibration_frame_t frame = {0};
        if (!ofdm_calibration_prepare_frame(1U, candidate, &frame)) {
            continue;
        }
        if (session_id == ofdm_calibration_make_session_id(
                              frame.kind, frame.tx_profile,
                              frame.rx_gain_index, frame.repeat_index,
                              frame.sequence)) {
            *sequence = candidate;
            return true;
        }
    }
    return false;
}

bool ofdm_calibration_parse_session_id(
    uint16_t session_id,
    uint8_t *kind,
    uint8_t *tx_profile,
    uint8_t *rx_gain_index,
    uint8_t *repeat_index)
{
    if (NULL == kind || NULL == tx_profile || NULL == rx_gain_index ||
        NULL == repeat_index ||
        OFDM_CAL_SESSION_PREFIX !=
            (session_id & OFDM_CAL_SESSION_PREFIX_MASK)) {
        return false;
    }
    *kind = (uint8_t)((session_id & OFDM_CAL_SESSION_KIND_MASK) >>
                      OFDM_CAL_SESSION_KIND_SHIFT);
    *tx_profile = (uint8_t)((session_id & OFDM_CAL_SESSION_TX_MASK) >>
                            OFDM_CAL_SESSION_TX_SHIFT);
    *rx_gain_index = (uint8_t)((session_id & OFDM_CAL_SESSION_RX_MASK) >>
                               OFDM_CAL_SESSION_RX_SHIFT);
    *repeat_index = (uint8_t)((session_id & OFDM_CAL_SESSION_REPEAT_MASK) >>
                              OFDM_CAL_SESSION_REPEAT_SHIFT);
    return valid_kind(*kind) &&
           OFDM_CAL_TX_PROFILE_COUNT > *tx_profile &&
           OFDM_CAL_RX_GAIN_COUNT > *rx_gain_index;
}

void ofdm_calibration_reset_stats(ofdm_calibration_stats_t *stats)
{
    if (NULL != stats) {
        memset(stats, 0, sizeof(*stats));
    }
}

static void increment_saturated(uint32_t *value)
{
    if (NULL != value && UINT32_MAX > *value) {
        ++(*value);
    }
}

void ofdm_calibration_record_sample(
    ofdm_calibration_stats_t *stats,
    uint8_t tx_profile,
    uint8_t rx_gain_index,
    bool phy_ok,
    bool crc_ok,
    uint32_t peak,
    uint32_t clip_samples,
    const ofdm_phy_frame_metrics_t *metrics)
{
    if (NULL == stats || OFDM_CAL_TX_PROFILE_COUNT <= tx_profile ||
        OFDM_CAL_RX_GAIN_COUNT <= rx_gain_index) {
        return;
    }
    ofdm_calibration_cell_t *cell = &stats->cells[tx_profile][rx_gain_index];
    increment_saturated(&cell->sample_count);
    if (phy_ok) {
        increment_saturated(&cell->phy_ok_count);
    }
    if (crc_ok) {
        increment_saturated(&cell->crc_ok_count);
    }
    if (UINT32_MAX - cell->clip_samples < clip_samples) {
        cell->clip_samples = UINT32_MAX;
    } else {
        cell->clip_samples += clip_samples;
    }
    if (cell->peak_max < peak) {
        cell->peak_max = peak;
    }
    if (NULL == metrics || !phy_ok) {
        return;
    }
    if (isfinite(metrics->header_evm_db) &&
        isfinite(metrics->payload_evm_db)) {
        increment_saturated(&cell->evm_count);
        cell->header_evm_sum_db += metrics->header_evm_db;
        cell->payload_evm_sum_db += metrics->payload_evm_db;
    }
    for (size_t group_index = 0U;
         group_index < OFDM_CAL_BAND_GROUP_COUNT; ++group_index) {
        const float value = metrics->channel_group_db[group_index];
        if (!isfinite(value) || OFDM_CAL_INVALID_DB >= value) {
            continue;
        }
        cell->band_db_sum[group_index] += value;
        increment_saturated(&cell->band_observation_count[group_index]);
    }
}

float ofdm_calibration_cell_crc_rate(
    const ofdm_calibration_cell_t *cell)
{
    if (NULL == cell || 0U == cell->sample_count) {
        return 0.0F;
    }
    return (float)cell->crc_ok_count / (float)cell->sample_count;
}

float ofdm_calibration_cell_phy_rate(
    const ofdm_calibration_cell_t *cell)
{
    if (NULL == cell || 0U == cell->sample_count) {
        return 0.0F;
    }
    return (float)cell->phy_ok_count / (float)cell->sample_count;
}

float ofdm_calibration_cell_payload_evm_db(
    const ofdm_calibration_cell_t *cell)
{
    if (NULL == cell || 0U == cell->evm_count) {
        return OFDM_CAL_INVALID_DB;
    }
    return cell->payload_evm_sum_db / (float)cell->evm_count;
}

float ofdm_calibration_cell_band_db(
    const ofdm_calibration_cell_t *cell,
    size_t group_index)
{
    if (NULL == cell || OFDM_CAL_BAND_GROUP_COUNT <= group_index ||
        0U == cell->band_observation_count[group_index]) {
        return OFDM_CAL_INVALID_DB;
    }
    return cell->band_db_sum[group_index] /
           (float)cell->band_observation_count[group_index];
}

float ofdm_calibration_cell_band_spread_db(
    const ofdm_calibration_cell_t *cell)
{
    if (NULL == cell) {
        return OFDM_CAL_INVALID_DB;
    }
    float minimum = 0.0F;
    float maximum = OFDM_CAL_INVALID_DB;
    bool observed = false;
    for (size_t group_index = 0U;
         group_index < OFDM_CAL_BAND_GROUP_COUNT; ++group_index) {
        const float value = ofdm_calibration_cell_band_db(cell, group_index);
        if (OFDM_CAL_INVALID_DB >= value) {
            continue;
        }
        if (!observed || value < minimum) {
            minimum = value;
        }
        if (!observed || value > maximum) {
            maximum = value;
        }
        observed = true;
    }
    return observed ? maximum - minimum : OFDM_CAL_INVALID_DB;
}

static bool cell_is_better(const ofdm_calibration_cell_t *candidate,
                           const ofdm_calibration_cell_t *current)
{
    if (NULL == candidate || NULL == current || 0U == candidate->sample_count ||
        0U == candidate->crc_ok_count || candidate->clip_samples > 0U) {
        return false;
    }
    if (0U == current->sample_count || 0U == current->crc_ok_count ||
        current->clip_samples > 0U) {
        return true;
    }
    const float candidate_crc = ofdm_calibration_cell_crc_rate(candidate);
    const float current_crc = ofdm_calibration_cell_crc_rate(current);
    if (candidate_crc != current_crc) {
        return candidate_crc > current_crc;
    }
    const float candidate_evm =
        ofdm_calibration_cell_payload_evm_db(candidate);
    const float current_evm =
        ofdm_calibration_cell_payload_evm_db(current);
    const bool candidate_has_evm = candidate->evm_count > 0U;
    const bool current_has_evm = current->evm_count > 0U;
    if (candidate_has_evm != current_has_evm) {
        return candidate_has_evm;
    }
    if (candidate_has_evm && candidate_evm != current_evm) {
        return candidate_evm < current_evm;
    }
    const float candidate_spread =
        ofdm_calibration_cell_band_spread_db(candidate);
    const float current_spread =
        ofdm_calibration_cell_band_spread_db(current);
    const bool candidate_has_spread =
        OFDM_CAL_INVALID_DB < candidate_spread;
    const bool current_has_spread = OFDM_CAL_INVALID_DB < current_spread;
    if (candidate_has_spread != current_has_spread) {
        return candidate_has_spread;
    }
    if (candidate_has_spread && candidate_spread != current_spread) {
        return candidate_spread < current_spread;
    }
    return false;
}

bool ofdm_calibration_select_recommendation(
    const ofdm_calibration_stats_t *stats,
    ofdm_calibration_recommendation_t *recommendation)
{
    if (NULL == stats || NULL == recommendation) {
        return false;
    }
    memset(recommendation, 0, sizeof(*recommendation));
    recommendation->first_bin = OFDM_FIRST_CARRIER_BIN;
    recommendation->last_bin = OFDM_LAST_CARRIER_BIN;

    const ofdm_calibration_cell_t *best_cell = NULL;
    uint8_t best_tx = 0U;
    uint8_t best_rx = 0U;
    for (uint8_t tx_index = 0U;
         tx_index < OFDM_CAL_TX_PROFILE_COUNT; ++tx_index) {
        for (uint8_t rx_index = 0U;
             rx_index < OFDM_CAL_RX_GAIN_COUNT; ++rx_index) {
            const ofdm_calibration_cell_t *candidate =
                &stats->cells[tx_index][rx_index];
            if (NULL == best_cell || cell_is_better(candidate, best_cell)) {
                if (0U < candidate->crc_ok_count &&
                    0U == candidate->clip_samples) {
                    best_cell = candidate;
                    best_tx = tx_index;
                    best_rx = rx_index;
                }
            }
        }
    }
    if (NULL == best_cell) {
        return false;
    }

    recommendation->valid = true;
    recommendation->tx_profile = best_tx;
    recommendation->rx_gain_index = best_rx;
    recommendation->crc_success_rate =
        ofdm_calibration_cell_crc_rate(best_cell);
    recommendation->phy_success_rate =
        ofdm_calibration_cell_phy_rate(best_cell);
    recommendation->payload_evm_db =
        ofdm_calibration_cell_payload_evm_db(best_cell);
    recommendation->band_spread_db =
        ofdm_calibration_cell_band_spread_db(best_cell);

    float strongest = OFDM_CAL_INVALID_DB;
    for (size_t group_index = 0U;
         group_index < OFDM_CAL_BAND_GROUP_COUNT; ++group_index) {
        const float value = ofdm_calibration_cell_band_db(best_cell,
                                                          group_index);
        recommendation->band_db[group_index] = value;
        if (value > strongest) {
            strongest = value;
        }
    }
    if (OFDM_CAL_INVALID_DB < strongest) {
        size_t best_start = 0U;
        size_t best_end = 0U;
        size_t current_start = OFDM_CAL_BAND_GROUP_COUNT;
        for (size_t group_index = 0U;
             group_index <= OFDM_CAL_BAND_GROUP_COUNT; ++group_index) {
            const bool usable = group_index < OFDM_CAL_BAND_GROUP_COUNT &&
                                 OFDM_CAL_INVALID_DB <
                                     recommendation->band_db[group_index] &&
                                 recommendation->band_db[group_index] >=
                                     strongest -
                                         OFDM_CAL_RECOMMEND_BAND_MARGIN_DB;
            if (usable) {
                if (OFDM_CAL_BAND_GROUP_COUNT == current_start) {
                    current_start = group_index;
                }
                continue;
            }
            if (OFDM_CAL_BAND_GROUP_COUNT != current_start) {
                if (group_index - current_start > best_end - best_start) {
                    best_start = current_start;
                    best_end = group_index;
                }
                current_start = OFDM_CAL_BAND_GROUP_COUNT;
            }
        }
        if (best_start < best_end) {
            uint16_t first_bin = 0U;
            uint16_t last_bin = 0U;
            if (ofdm_phy_get_response_group_range(best_start,
                                                  &first_bin, &last_bin) &&
                ofdm_phy_get_response_group_range(best_end - 1U,
                                                  NULL, &last_bin)) {
                recommendation->first_bin = first_bin;
                recommendation->last_bin = last_bin;
            }
        }
    }
    return true;
}
