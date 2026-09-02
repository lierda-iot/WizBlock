/*
 * ESP camera staged-DVP override verified by the C16 investigation.
 *
 * This keeps the managed esp_cam_sensor v2.4.0 implementation while raising
 * its internal ping-pong buffer to a frame-divisible size.  It is used by the
 * formal Tank capture path; Remote never calls it.
 */
#pragma once

#include "esp_cam_ctlr_dvp.h"

#ifdef __cplusplus
extern "C" {
#endif

#define RC_DVP_STAGED_DMA_BUFFER_BYTES 102400U
#define RC_DVP_STAGED_DMA_HALF_BYTES 51200U
#define RC_DVP_STAGED_COPIED_BLOCKS_PER_FRAME 11U
#define RC_DVP_STAGED_BLOCK_HIST_SIZE 13U
#define RC_DVP_STAGED_EOF_BLOCK_HIST_SIZE 12U

typedef struct {
    uint32_t sync_end_complete_candidates;
    uint32_t sync_end_incomplete_events;
    uint32_t sync_end_received_blocks[RC_DVP_STAGED_BLOCK_HIST_SIZE];
    uint32_t sync_end_recovered_penultimate;
    uint32_t recv_data_events;
    uint32_t eof_desc_matches_source;
    uint32_t eof_desc_mismatches_source;
    uint32_t eof_desc_invalid;
    uint32_t eof_desc_mismatch_by_block[RC_DVP_STAGED_EOF_BLOCK_HIST_SIZE];
    uint32_t control_rx_events;
    uint32_t incomplete_within_2ms_of_control;
    uint32_t incomplete_within_10ms_of_control;
    uint32_t incomplete_after_10ms_of_control;
} rc_dvp_staged_stats_t;

esp_err_t rc_cam_new_dvp_ctlr_staged(
    const esp_cam_ctlr_dvp_config_t *config,
    esp_cam_ctlr_handle_t *ret_handle);

esp_err_t rc_cam_del_dvp_ctlr_staged(esp_cam_ctlr_handle_t handle);

esp_err_t rc_cam_ctlr_dvp_init_staged(
    int ctlr_id,
    cam_clock_source_t clk_src,
    const esp_cam_ctlr_dvp_pin_config_t *pin);

esp_err_t rc_dvp_staged_prepare_resume(void);

void rc_dvp_staged_request_quiesce(void);

esp_err_t rc_dvp_staged_wait_quiesced(uint32_t timeout_ms);

void rc_dvp_staged_get_stats(rc_dvp_staged_stats_t *stats);

void rc_dvp_staged_note_control_rx(void);

#ifdef __cplusplus
}
#endif
