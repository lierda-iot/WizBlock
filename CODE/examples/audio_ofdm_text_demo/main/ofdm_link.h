#ifndef OFDM_LINK_H
#define OFDM_LINK_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "esp_err.h"

#include "ofdm_utf8.h"

#define OFDM_LINK_STATUS_TEXT_BYTES 64U
#define OFDM_LINK_CHIRP_SCORE_MAX_MILLI 1000U

typedef enum {
    OFDM_LINK_STATE_BOOT = 0,
    OFDM_LINK_STATE_IDLE_RX,
    OFDM_LINK_STATE_RX_SYNC,
    OFDM_LINK_STATE_RX_DATA,
    OFDM_LINK_STATE_RX_OK,
    OFDM_LINK_STATE_RX_ERROR,
    OFDM_LINK_STATE_TX_DATA,
    OFDM_LINK_STATE_TX_DONE,
    OFDM_LINK_STATE_ERROR,
} ofdm_link_state_t;

typedef struct {
    ofdm_link_state_t state;
    uint16_t session_id;
    uint16_t message_bytes;
    uint16_t received_bitmap;
    uint8_t frame_index;
    uint8_t frame_count;
    uint8_t progress_percent;
    bool audio_sent;
    char status[OFDM_LINK_STATUS_TEXT_BYTES];
    char message[OFDM_MESSAGE_MAX_BYTES + 1U];
} ofdm_link_snapshot_t;

typedef struct {
    uint32_t rx_drop;
    uint32_t rx_queue_peak;
    uint32_t tx_underrun;
    uint32_t sync_ok;
    uint32_t sync_fail;
    uint32_t rs_fixed;
    uint32_t crc_fail;
    uint32_t dsp_us_max;
    uint32_t chirp_hits;
    uint32_t rx_mean_square_max;
    uint32_t rx_peak_max;
    uint32_t rx_clip_samples;
    uint16_t chirp_score_max_milli;
} ofdm_link_health_t;

esp_err_t ofdm_link_init(void);
esp_err_t ofdm_link_start(void);
bool ofdm_link_request_send(void);
bool ofdm_link_request_tx_calibration(void);
bool ofdm_link_request_rx_calibration(void);
bool ofdm_link_request_stop_calibration(void);
bool ofdm_link_get_snapshot(ofdm_link_snapshot_t *snapshot);
void ofdm_link_take_health(ofdm_link_health_t *health);
const char *ofdm_link_state_name(ofdm_link_state_t state);

#endif
