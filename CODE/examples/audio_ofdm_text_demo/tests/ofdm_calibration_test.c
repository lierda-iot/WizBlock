#include "ofdm_calibration.h"

#include "ofdm_frame.h"

#include <assert.h>
#include <math.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#define TEST_RUN_ID UINT16_C(0x2345)
#define TEST_FLOAT_EPSILON 0.0001F

static void assert_float_equal(float expected, float actual)
{
    assert(TEST_FLOAT_EPSILON > fabsf(expected - actual));
}

static ofdm_phy_frame_metrics_t make_metrics(float payload_evm_db,
                                              const float *band_db)
{
    ofdm_phy_frame_metrics_t metrics = {0};
    metrics.header_evm_db = payload_evm_db - 1.0F;
    metrics.payload_evm_db = payload_evm_db;
    for (size_t index = 0U;
         index < OFDM_CAL_BAND_GROUP_COUNT; ++index) {
        metrics.channel_group_db[index] = NULL == band_db
                                              ? -3.0F
                                              : band_db[index];
    }
    return metrics;
}

static void test_profile_tables(void)
{
    static const uint8_t expected_volume[OFDM_CAL_TX_PROFILE_COUNT] = {
        70U, 80U, 90U, 90U,
    };
    static const uint16_t expected_pcm[OFDM_CAL_TX_PROFILE_COUNT] = {
        14000U, 20000U, 24000U, 28000U,
    };
    static const float expected_gain[OFDM_CAL_RX_GAIN_COUNT] = {
        24.0F, 30.0F, 34.5F, 37.5F,
    };

    assert(130000U == OFDM_CAL_TIMEOUT_MS);
    assert(2U == OFDM_NORMAL_TX_PROFILE_INDEX);
    assert(1U == OFDM_CAL_DEFAULT_RX_GAIN_INDEX);
    for (size_t index = 0U; index < OFDM_CAL_TX_PROFILE_COUNT; ++index) {
        const ofdm_calibration_tx_profile_t *profile =
            ofdm_calibration_get_tx_profile(index);
        assert(NULL != profile);
        assert(expected_volume[index] == profile->volume_percent);
        assert(expected_pcm[index] == profile->pcm_scale);
    }
    assert(NULL ==
           ofdm_calibration_get_tx_profile(OFDM_CAL_TX_PROFILE_COUNT));

    for (size_t index = 0U; index < OFDM_CAL_RX_GAIN_COUNT; ++index) {
        float gain_db = 0.0F;
        assert(ofdm_calibration_get_rx_gain(index, &gain_db));
        assert_float_equal(expected_gain[index], gain_db);
    }
    assert(!ofdm_calibration_get_rx_gain(0U, NULL));
    float invalid_gain = 0.0F;
    assert(!ofdm_calibration_get_rx_gain(OFDM_CAL_RX_GAIN_COUNT,
                                         &invalid_gain));

    const ofdm_calibration_tx_profile_t *normal_tx =
        ofdm_calibration_get_tx_profile(OFDM_NORMAL_TX_PROFILE_INDEX);
    float normal_gain_db = 0.0F;
    assert(NULL != normal_tx);
    assert(90U == normal_tx->volume_percent);
    assert(24000U == normal_tx->pcm_scale);
    assert(ofdm_calibration_get_rx_gain(OFDM_CAL_DEFAULT_RX_GAIN_INDEX,
                                        &normal_gain_db));
    assert_float_equal(30.0F, normal_gain_db);
}

static void assert_control_or_sample_layout(
    const ofdm_calibration_frame_t *frame,
    uint16_t sequence)
{
    assert(NULL != frame);
    const uint16_t combination_sequence = (uint16_t)(sequence - 1U);
    const uint8_t slot = (uint8_t)(combination_sequence %
                                   OFDM_CAL_FRAMES_PER_COMBINATION);
    const uint8_t combination = (uint8_t)(combination_sequence /
                                          OFDM_CAL_FRAMES_PER_COMBINATION);
    const uint8_t expected_tx = (uint8_t)(combination /
                                          OFDM_CAL_RX_GAIN_COUNT);
    const uint8_t expected_rx = (uint8_t)(combination %
                                          OFDM_CAL_RX_GAIN_COUNT);

    assert(expected_tx == frame->tx_profile);
    assert(expected_rx == frame->rx_gain_index);
    if (0U == slot) {
        assert(OFDM_CAL_FRAME_CONTROL == frame->kind);
        assert(0U == frame->repeat_index);
        assert(0U == frame->repeat_count);
    } else {
        assert(OFDM_CAL_FRAME_SAMPLE == frame->kind);
        assert((uint8_t)(slot - 1U) == frame->repeat_index);
        assert(OFDM_CAL_SAMPLE_REPEAT_COUNT == frame->repeat_count);
    }
}

static void test_complete_sequence_and_round_trip(void)
{
    uint32_t kind_counts[OFDM_CAL_FRAME_END + 1U] = {0};
    bool seen_combination[OFDM_CAL_TX_PROFILE_COUNT]
                         [OFDM_CAL_RX_GAIN_COUNT] = {{false}};

    assert(50U == OFDM_CAL_TOTAL_FRAME_COUNT);
    for (uint16_t sequence = 0U;
         sequence < OFDM_CAL_TOTAL_FRAME_COUNT; ++sequence) {
        uint8_t payload[OFDM_CAL_PAYLOAD_BYTES] = {0};
        ofdm_calibration_frame_t frame = {0};
        ofdm_calibration_frame_t decoded = {0};

        assert(ofdm_calibration_prepare_frame(TEST_RUN_ID, sequence,
                                              &frame));
        assert(TEST_RUN_ID == frame.run_id);
        assert(sequence == frame.sequence);
        assert(OFDM_CAL_TOTAL_FRAME_COUNT == frame.total_frames);
        assert(OFDM_FIRST_CARRIER_BIN == frame.first_bin);
        assert(OFDM_LAST_CARRIER_BIN == frame.last_bin);
        assert(frame.kind <= OFDM_CAL_FRAME_END);
        ++kind_counts[frame.kind];

        if (0U == sequence) {
            assert(OFDM_CAL_FRAME_START == frame.kind);
            assert(OFDM_CAL_SAFE_TX_PROFILE_INDEX == frame.tx_profile);
            assert(OFDM_CAL_DEFAULT_RX_GAIN_INDEX == frame.rx_gain_index);
        } else if (OFDM_CAL_TOTAL_FRAME_COUNT - 1U == sequence) {
            assert(OFDM_CAL_FRAME_END == frame.kind);
            assert(OFDM_CAL_SAFE_TX_PROFILE_INDEX == frame.tx_profile);
            assert(OFDM_CAL_DEFAULT_RX_GAIN_INDEX == frame.rx_gain_index);
        } else {
            assert_control_or_sample_layout(&frame, sequence);
            if (OFDM_CAL_FRAME_CONTROL == frame.kind) {
                assert(!seen_combination[frame.tx_profile]
                                        [frame.rx_gain_index]);
                seen_combination[frame.tx_profile]
                                [frame.rx_gain_index] = true;
            }
        }

        const ofdm_calibration_tx_profile_t *profile =
            ofdm_calibration_get_tx_profile(frame.tx_profile);
        assert(NULL != profile);
        assert(profile->volume_percent == frame.volume_percent);
        assert(profile->pcm_scale == frame.pcm_scale);

        assert(ofdm_calibration_encode(&frame, payload));
        assert(ofdm_calibration_decode(payload, &decoded));
        assert(0 == memcmp(&frame, &decoded, sizeof(frame)));

        const uint16_t session_id = ofdm_calibration_make_session_id(
            frame.kind, frame.tx_profile, frame.rx_gain_index,
            frame.repeat_index, frame.sequence);
        assert(ofdm_calibration_session_matches(session_id, &decoded));
        assert(!ofdm_calibration_session_matches((uint16_t)(session_id ^ 1U),
                                                 &decoded));
    }

    assert(1U == kind_counts[OFDM_CAL_FRAME_START]);
    assert(16U == kind_counts[OFDM_CAL_FRAME_CONTROL]);
    assert(32U == kind_counts[OFDM_CAL_FRAME_SAMPLE]);
    assert(1U == kind_counts[OFDM_CAL_FRAME_END]);
    for (size_t tx_index = 0U;
         tx_index < OFDM_CAL_TX_PROFILE_COUNT; ++tx_index) {
        for (size_t rx_index = 0U;
             rx_index < OFDM_CAL_RX_GAIN_COUNT; ++rx_index) {
            assert(seen_combination[tx_index][rx_index]);
        }
    }

    ofdm_calibration_frame_t invalid = {0};
    assert(!ofdm_calibration_prepare_frame(0U, 0U, &invalid));
    assert(!ofdm_calibration_prepare_frame(
        TEST_RUN_ID, OFDM_CAL_TOTAL_FRAME_COUNT, &invalid));
    assert(!ofdm_calibration_prepare_frame(TEST_RUN_ID, 0U, NULL));
}

static void test_end_sequence_detection(void)
{
    uint16_t physical_frame_count = 0U;
    for (uint16_t sequence = 0U;
         sequence < OFDM_CAL_TOTAL_FRAME_COUNT; ++sequence) {
        ofdm_calibration_frame_t frame = {0};
        assert(ofdm_calibration_prepare_frame(TEST_RUN_ID, sequence,
                                               &frame));
        const bool expected_end =
            OFDM_CAL_TOTAL_FRAME_COUNT - 1U == sequence;
        assert(expected_end == ofdm_calibration_is_end_sequence(sequence));
        const uint8_t transmit_count =
            ofdm_calibration_transmit_count(sequence);
        const uint8_t expected_count =
            expected_end ? OFDM_CAL_END_TRANSMIT_COUNT :
            OFDM_CAL_FRAME_START == frame.kind
                ? OFDM_CAL_START_TRANSMIT_COUNT
                : OFDM_CAL_FRAME_CONTROL == frame.kind
                      ? OFDM_CAL_CONTROL_TRANSMIT_COUNT
                      : 1U;
        assert(expected_count == transmit_count);
        assert((OFDM_CAL_FRAME_START == frame.kind ||
                OFDM_CAL_FRAME_CONTROL == frame.kind || expected_end) ==
               ofdm_calibration_is_retransmittable_sequence(sequence));
        physical_frame_count =
            (uint16_t)(physical_frame_count + transmit_count);
    }
    assert((OFDM_CAL_START_TRANSMIT_COUNT +
            OFDM_CAL_TX_PROFILE_COUNT * OFDM_CAL_RX_GAIN_COUNT *
                    (OFDM_CAL_CONTROL_TRANSMIT_COUNT +
                     OFDM_CAL_SAMPLE_REPEAT_COUNT) +
                OFDM_CAL_END_TRANSMIT_COUNT) == physical_frame_count);
    assert(!ofdm_calibration_is_end_sequence(
        OFDM_CAL_TOTAL_FRAME_COUNT));
    assert(0U ==
           ofdm_calibration_transmit_count(OFDM_CAL_TOTAL_FRAME_COUNT));
    assert(!ofdm_calibration_is_retransmittable_sequence(
        OFDM_CAL_TOTAL_FRAME_COUNT));
}

static void test_header_only_recovery_policy(void)
{
    uint16_t session_ids[OFDM_CAL_TOTAL_FRAME_COUNT] = {0};

    for (uint16_t sequence = 0U;
         sequence < OFDM_CAL_TOTAL_FRAME_COUNT; ++sequence) {
        ofdm_calibration_frame_t frame = {0};
        assert(ofdm_calibration_prepare_frame(TEST_RUN_ID, sequence,
                                               &frame));
        const uint16_t session_id = ofdm_calibration_make_session_id(
            frame.kind, frame.tx_profile, frame.rx_gain_index,
            frame.repeat_index, frame.sequence);
        uint16_t recovered_sequence = UINT16_MAX;
        assert(ofdm_calibration_find_sequence(session_id,
                                              &recovered_sequence));
        assert(sequence == recovered_sequence);

        const bool expected_header_only =
            OFDM_CAL_FRAME_CONTROL == frame.kind ||
            OFDM_CAL_FRAME_END == frame.kind;
        assert(expected_header_only ==
               ofdm_calibration_is_header_recoverable_sequence(sequence));

        for (uint16_t previous = 0U; previous < sequence; ++previous) {
            assert(session_ids[previous] != session_id);
        }
        session_ids[sequence] = session_id;
    }

    uint16_t sequence = 0U;
    assert(!ofdm_calibration_find_sequence(0U, &sequence));
    assert(!ofdm_calibration_find_sequence(session_ids[0], NULL));
    assert(!ofdm_calibration_is_header_recoverable_sequence(
        OFDM_CAL_TOTAL_FRAME_COUNT));
}

static void test_start_recovery_candidates(void)
{
    ofdm_calibration_frame_t frame = {0};

    assert(ofdm_calibration_prepare_frame(TEST_RUN_ID, 0U, &frame));
    assert(!ofdm_calibration_can_recover_start(&frame));
    assert(ofdm_calibration_prepare_frame(TEST_RUN_ID, 1U, &frame));
    assert(ofdm_calibration_can_recover_start(&frame));
    assert(ofdm_calibration_prepare_frame(TEST_RUN_ID, 2U, &frame));
    assert(ofdm_calibration_can_recover_start(&frame));
    assert(ofdm_calibration_prepare_frame(
        TEST_RUN_ID, OFDM_CAL_TOTAL_FRAME_COUNT - 2U, &frame));
    assert(ofdm_calibration_can_recover_start(&frame));
    assert(ofdm_calibration_prepare_frame(
        TEST_RUN_ID, OFDM_CAL_TOTAL_FRAME_COUNT - 1U, &frame));
    assert(!ofdm_calibration_can_recover_start(&frame));

    frame.kind = OFDM_CAL_FRAME_SAMPLE;
    assert(!ofdm_calibration_can_recover_start(&frame));
    assert(!ofdm_calibration_can_recover_start(NULL));
}

static void test_invalid_protocol_values(void)
{
    uint8_t payload[OFDM_CAL_PAYLOAD_BYTES] = {0};
    ofdm_calibration_frame_t frame = {0};
    ofdm_calibration_frame_t decoded = {0};

    assert(ofdm_calibration_prepare_frame(TEST_RUN_ID, 1U, &frame));
    assert(ofdm_calibration_encode(&frame, payload));

    uint8_t corrupted[OFDM_CAL_PAYLOAD_BYTES] = {0};
    memcpy(corrupted, payload, sizeof(corrupted));
    corrupted[4] = (uint8_t)(OFDM_CAL_VERSION + 1U);
    assert(!ofdm_calibration_decode(corrupted, &decoded));

    memcpy(corrupted, payload, sizeof(corrupted));
    corrupted[6] = OFDM_CAL_TX_PROFILE_COUNT;
    assert(!ofdm_calibration_decode(corrupted, &decoded));

    memcpy(corrupted, payload, sizeof(corrupted));
    corrupted[7] = OFDM_CAL_RX_GAIN_COUNT;
    assert(!ofdm_calibration_decode(corrupted, &decoded));

    memcpy(corrupted, payload, sizeof(corrupted));
    corrupted[11] = 1U;
    assert(!ofdm_calibration_decode(corrupted, &decoded));

    ofdm_calibration_frame_t wrong_layout = frame;
    wrong_layout.kind = OFDM_CAL_FRAME_SAMPLE;
    wrong_layout.repeat_count = OFDM_CAL_SAMPLE_REPEAT_COUNT;
    assert(!ofdm_calibration_encode(&wrong_layout, corrupted));

    wrong_layout = frame;
    wrong_layout.tx_profile = 1U;
    const ofdm_calibration_tx_profile_t *profile =
        ofdm_calibration_get_tx_profile(wrong_layout.tx_profile);
    assert(NULL != profile);
    wrong_layout.volume_percent = profile->volume_percent;
    wrong_layout.pcm_scale = profile->pcm_scale;
    assert(!ofdm_calibration_encode(&wrong_layout, corrupted));

    wrong_layout = frame;
    wrong_layout.sequence = 2U;
    assert(!ofdm_calibration_encode(&wrong_layout, corrupted));

    uint8_t kind = 0U;
    uint8_t tx_profile = 0U;
    uint8_t rx_gain = 0U;
    uint8_t repeat = 0U;
    const uint16_t session_id = ofdm_calibration_make_session_id(
        frame.kind, frame.tx_profile, frame.rx_gain_index,
        frame.repeat_index, frame.sequence);
    assert(ofdm_calibration_parse_session_id(
        session_id, &kind, &tx_profile, &rx_gain, &repeat));
    assert(frame.kind == kind);
    assert(frame.tx_profile == tx_profile);
    assert(frame.rx_gain_index == rx_gain);
    assert(frame.repeat_index == repeat);
    assert(!ofdm_calibration_parse_session_id(0U, &kind, &tx_profile,
                                              &rx_gain, &repeat));
}

static void test_calibration_frame_isolation(void)
{
    uint8_t payload[OFDM_CAL_PAYLOAD_BYTES] = {0};
    uint8_t output[OFDM_MESSAGE_MAX_BYTES + 1U] = {0};
    size_t output_length = 0U;
    ofdm_calibration_frame_t calibration = {0};
    ofdm_frame_t frame = {0};
    ofdm_reassembly_t reassembly = {0};

    assert(ofdm_calibration_prepare_frame(TEST_RUN_ID, 0U, &calibration));
    assert(ofdm_calibration_encode(&calibration, payload));
    const uint16_t session_id = ofdm_calibration_make_session_id(
        calibration.kind, calibration.tx_profile,
        calibration.rx_gain_index, calibration.repeat_index,
        calibration.sequence);
    assert(OFDM_FRAME_OK == ofdm_frame_build_calibration(
                                payload, sizeof(payload), session_id,
                                &frame));
    assert(ofdm_frame_is_calibration(&frame.header));
    assert(OFDM_FRAME_OK == ofdm_frame_validate(&frame));
    assert(OFDM_REASSEMBLY_REJECTED == ofdm_reassembly_accept(
                                             &reassembly, &frame, output,
                                             sizeof(output), &output_length));
    assert(!reassembly.active);
    assert(0U == output_length);

    const uint8_t ordinary_message[] = {'O', 'K'};
    assert(OFDM_FRAME_OK == ofdm_frame_build(
                                ordinary_message, sizeof(ordinary_message),
                                7U, 0U, &frame));
    assert(!ofdm_frame_is_calibration(&frame.header));
    assert(OFDM_REASSEMBLY_COMPLETE == ofdm_reassembly_accept(
                                             &reassembly, &frame, output,
                                             sizeof(output), &output_length));
    assert(sizeof(ordinary_message) == output_length);
    assert(0 == memcmp(ordinary_message, output, output_length));
}

static void record_sample(ofdm_calibration_stats_t *stats,
                          uint8_t tx_profile,
                          uint8_t rx_gain,
                          bool phy_ok,
                          bool crc_ok,
                          uint32_t clip_samples,
                          float evm_db,
                          const float *band_db)
{
    const ofdm_phy_frame_metrics_t metrics = make_metrics(evm_db, band_db);
    ofdm_calibration_record_sample(stats, tx_profile, rx_gain, phy_ok,
                                   crc_ok, 1234U, clip_samples,
                                   phy_ok ? &metrics : NULL);
}

static void test_stats_and_clipping_priority(void)
{
    ofdm_calibration_stats_t stats = {0};
    ofdm_calibration_recommendation_t recommendation = {0};

    record_sample(&stats, 0U, 0U, true, true, 1U, -30.0F, NULL);
    record_sample(&stats, 1U, 0U, true, true, 0U, -10.0F, NULL);

    const ofdm_calibration_cell_t *clipped = &stats.cells[0][0];
    assert(1U == clipped->sample_count);
    assert(1U == clipped->phy_ok_count);
    assert(1U == clipped->crc_ok_count);
    assert(1U == clipped->clip_samples);
    assert(1234U == clipped->peak_max);
    assert_float_equal(1.0F, ofdm_calibration_cell_crc_rate(clipped));
    assert_float_equal(-30.0F,
                       ofdm_calibration_cell_payload_evm_db(clipped));

    assert(ofdm_calibration_select_recommendation(&stats,
                                                  &recommendation));
    assert(recommendation.valid);
    assert(1U == recommendation.tx_profile);
    assert(0U == recommendation.rx_gain_index);

    ofdm_calibration_reset_stats(&stats);
    memset(&recommendation, 0, sizeof(recommendation));
    record_sample(&stats, 0U, 0U, true, true, 2U, -30.0F, NULL);
    assert(!ofdm_calibration_select_recommendation(&stats,
                                                   &recommendation));
}

static void test_crc_then_evm_then_flatness_order(void)
{
    ofdm_calibration_stats_t stats = {0};
    ofdm_calibration_recommendation_t recommendation = {0};

    record_sample(&stats, 0U, 0U, true, true, 0U, -40.0F, NULL);
    record_sample(&stats, 0U, 0U, true, false, 0U, -40.0F, NULL);
    record_sample(&stats, 1U, 0U, true, true, 0U, -10.0F, NULL);
    assert(ofdm_calibration_select_recommendation(&stats,
                                                  &recommendation));
    assert(1U == recommendation.tx_profile);

    ofdm_calibration_reset_stats(&stats);
    memset(&recommendation, 0, sizeof(recommendation));
    record_sample(&stats, 0U, 0U, true, true, 0U, -10.0F, NULL);
    record_sample(&stats, 0U, 0U, true, false, 0U, -10.0F, NULL);
    record_sample(&stats, 1U, 0U, true, true, 0U, -25.0F, NULL);
    record_sample(&stats, 1U, 0U, false, false, 0U, -25.0F, NULL);
    assert(ofdm_calibration_select_recommendation(&stats,
                                                  &recommendation));
    assert(1U == recommendation.tx_profile);
    assert_float_equal(0.5F, recommendation.crc_success_rate);
    assert_float_equal(-25.0F, recommendation.payload_evm_db);

    static const float uneven_bands[OFDM_CAL_BAND_GROUP_COUNT] = {
        0.0F, -8.0F, -2.0F, -7.0F, -1.0F, -6.0F, -3.0F, -5.0F,
    };
    static const float flat_bands[OFDM_CAL_BAND_GROUP_COUNT] = {
        -3.0F, -2.0F, -3.0F, -2.0F, -3.0F, -2.0F, -3.0F, -2.0F,
    };
    ofdm_calibration_reset_stats(&stats);
    memset(&recommendation, 0, sizeof(recommendation));
    record_sample(&stats, 0U, 0U, true, true, 0U, -20.0F,
                  uneven_bands);
    record_sample(&stats, 1U, 0U, true, true, 0U, -20.0F, flat_bands);
    assert(ofdm_calibration_select_recommendation(&stats,
                                                  &recommendation));
    assert(1U == recommendation.tx_profile);
    assert_float_equal(1.0F, recommendation.band_spread_db);
}

static void test_longest_usable_band_range(void)
{
    static const float bands[OFDM_CAL_BAND_GROUP_COUNT] = {
        -20.0F, -5.0F, 0.0F, -4.0F, -30.0F, -1.0F, -3.0F, -4.0F,
    };
    ofdm_calibration_stats_t stats = {0};
    ofdm_calibration_recommendation_t recommendation = {0};

    record_sample(&stats, 2U, 3U, true, true, 0U, -18.0F, bands);
    assert(ofdm_calibration_select_recommendation(&stats,
                                                  &recommendation));
    assert(2U == recommendation.tx_profile);
    assert(3U == recommendation.rx_gain_index);
    assert(35U == recommendation.first_bin);
    assert(45U == recommendation.last_bin);
    for (size_t index = 0U;
         index < OFDM_CAL_BAND_GROUP_COUNT; ++index) {
        assert_float_equal(bands[index], recommendation.band_db[index]);
    }

    const ofdm_calibration_cell_t *cell = &stats.cells[2][3];
    assert_float_equal(bands[0], ofdm_calibration_cell_band_db(cell, 0U));
    assert(OFDM_PHY_RESPONSE_INVALID_DB ==
           ofdm_calibration_cell_band_db(cell,
                                         OFDM_CAL_BAND_GROUP_COUNT));
}

int main(void)
{
    test_profile_tables();
    test_complete_sequence_and_round_trip();
    test_end_sequence_detection();
    test_header_only_recovery_policy();
    test_start_recovery_candidates();
    test_invalid_protocol_values();
    test_calibration_frame_isolation();
    test_stats_and_clipping_priority();
    test_crc_then_evm_then_flatness_order();
    test_longest_usable_band_range();
    puts("ofdm_calibration_test: PASS");
    return 0;
}
