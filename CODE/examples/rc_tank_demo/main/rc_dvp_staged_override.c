/*
 * SPDX-FileCopyrightText: 2026 Espressif Systems (Shanghai) CO LTD
 * SPDX-License-Identifier: Apache-2.0
 *
 * Compile the locked esp_cam_sensor v2.4.0 extended DVP driver a second time
 * under project-local symbol names.  The internal staging size remains
 * 102400 bytes (two 51200-byte halves). Each completed half is copied in the
 * GDMA EOF ISR using the proven software source index, so queued type-only
 * events cannot later read a half that DMA has already reused. The upstream
 * final-half copy contract remains unchanged.
 */

#include "sdkconfig.h"
#include "rc_dvp_staged_override.h"

#if defined(CONFIG_RC_TANK_ROLE_TANK) && \
    defined(CONFIG_RC_TANK_STABLE_CAPTURE)

#include <stdbool.h>
#include <stdint.h>
#include <string.h>
#include "driver/gpio.h"
#include "esp_attr.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "hal/gpio_ll.h"
#include "soc/gpio_struct.h"
#include "esp_private/gdma.h"

static BaseType_t rc_dvp_staged_queue_send_from_isr(
    QueueHandle_t queue,
    const void *item,
    BaseType_t *higher_priority_task_woken);
static BaseType_t rc_dvp_staged_queue_receive(
    QueueHandle_t queue,
    void *item,
    TickType_t ticks_to_wait);
static esp_err_t rc_dvp_staged_gpio_intr_enable(gpio_num_t gpio_num);
static esp_err_t rc_dvp_staged_gdma_register_rx_event_callbacks(
    gdma_channel_handle_t dma_chan,
    gdma_rx_event_callbacks_t *cbs,
    void *user_data);

#undef CONFIG_CAM_CTRL_DVP_DMA_BUFFER_SIZE
#define CONFIG_CAM_CTRL_DVP_DMA_BUFFER_SIZE RC_DVP_STAGED_DMA_BUFFER_BYTES

#define esp_cam_new_dvp_ctlr_ext rc_cam_new_dvp_ctlr_staged_impl
#define esp_cam_ctlr_dvp_init_ext rc_cam_ctlr_dvp_init_staged_impl
#define gpio_intr_enable rc_dvp_staged_gpio_intr_enable
#define gdma_register_rx_event_callbacks \
    rc_dvp_staged_gdma_register_rx_event_callbacks

#undef xQueueSendFromISR
#define xQueueSendFromISR(queue, item, higher_priority_task_woken) \
    rc_dvp_staged_queue_send_from_isr(                         \
        (queue), (item), (higher_priority_task_woken))
#undef xQueueReceive
#define xQueueReceive(queue, item, ticks_to_wait) \
    rc_dvp_staged_queue_receive((queue), (item), (ticks_to_wait))

#include "../managed_components/espressif__esp_cam_sensor/src/driver_dvp/esp_cam_ctlr_dvp_cam.c"

#undef gdma_register_rx_event_callbacks
#undef gpio_intr_enable
#undef xQueueReceive
#undef xQueueSendFromISR
#undef esp_cam_ctlr_dvp_init_ext
#undef esp_cam_new_dvp_ctlr_ext

static dvp_cam_ctlr_t *s_rc_dvp_staged_ctlr = NULL;
static volatile uint32_t s_rc_dvp_sync_end_complete_candidates = 0U;
static volatile uint32_t s_rc_dvp_sync_end_incomplete_events = 0U;
static volatile uint32_t s_rc_dvp_sync_end_received_blocks[
    RC_DVP_STAGED_BLOCK_HIST_SIZE] = {0};
static volatile uint32_t s_rc_dvp_sync_end_recovered_penultimate = 0U;
static volatile uint32_t s_rc_dvp_recv_data_events = 0U;
static volatile uint32_t s_rc_dvp_eof_desc_matches_source = 0U;
static volatile uint32_t s_rc_dvp_eof_desc_mismatches_source = 0U;
static volatile uint32_t s_rc_dvp_eof_desc_invalid = 0U;
static volatile uint32_t s_rc_dvp_eof_desc_mismatch_by_block[
    RC_DVP_STAGED_EOF_BLOCK_HIST_SIZE] = {0};
static volatile uint32_t s_rc_dvp_control_rx_events = 0U;
static volatile int64_t s_rc_dvp_last_control_rx_us = 0;
static volatile uint32_t s_rc_dvp_incomplete_within_2ms = 0U;
static volatile uint32_t s_rc_dvp_incomplete_within_10ms = 0U;
static volatile uint32_t s_rc_dvp_incomplete_after_10ms = 0U;
static volatile intptr_t s_rc_dvp_last_eof_desc_addr = 0;
static gdma_event_callback_t s_rc_dvp_upstream_recv_eof = NULL;
static volatile bool s_rc_dvp_quiesce_hold_requested = false;
static volatile bool s_rc_dvp_quiesce_hold_acknowledged = false;

static bool IRAM_ATTR rc_dvp_staged_recv_eof_probe(
    gdma_channel_handle_t dma_chan,
    gdma_event_data_t *event_data,
    void *user_data)
{
    if (NULL != event_data) {
        s_rc_dvp_last_eof_desc_addr = event_data->rx_eof_desc_addr;
    }
    return (NULL != s_rc_dvp_upstream_recv_eof) &&
           s_rc_dvp_upstream_recv_eof(dma_chan, event_data, user_data);
}

static esp_err_t rc_dvp_staged_gdma_register_rx_event_callbacks(
    gdma_channel_handle_t dma_chan,
    gdma_rx_event_callbacks_t *cbs,
    void *user_data)
{
    if ((NULL == cbs) || (NULL == cbs->on_recv_eof)) {
        return ESP_ERR_INVALID_ARG;
    }
    gdma_rx_event_callbacks_t wrapped = *cbs;
    s_rc_dvp_upstream_recv_eof = cbs->on_recv_eof;
    wrapped.on_recv_eof = rc_dvp_staged_recv_eof_probe;
    return gdma_register_rx_event_callbacks(dma_chan, &wrapped, user_data);
}

static void rc_dvp_staged_reset_stats(void)
{
    s_rc_dvp_sync_end_complete_candidates = 0U;
    s_rc_dvp_sync_end_incomplete_events = 0U;
    memset((void *)s_rc_dvp_sync_end_received_blocks, 0,
           sizeof(s_rc_dvp_sync_end_received_blocks));
    s_rc_dvp_sync_end_recovered_penultimate = 0U;
    s_rc_dvp_recv_data_events = 0U;
    s_rc_dvp_eof_desc_matches_source = 0U;
    s_rc_dvp_eof_desc_mismatches_source = 0U;
    s_rc_dvp_eof_desc_invalid = 0U;
    memset((void *)s_rc_dvp_eof_desc_mismatch_by_block, 0,
           sizeof(s_rc_dvp_eof_desc_mismatch_by_block));
    s_rc_dvp_control_rx_events = 0U;
    s_rc_dvp_last_control_rx_us = 0;
    s_rc_dvp_incomplete_within_2ms = 0U;
    s_rc_dvp_incomplete_within_10ms = 0U;
    s_rc_dvp_incomplete_after_10ms = 0U;
    s_rc_dvp_last_eof_desc_addr = 0;
    s_rc_dvp_quiesce_hold_requested = false;
    s_rc_dvp_quiesce_hold_acknowledged = false;
}

static esp_err_t rc_dvp_staged_clear_pending_vsync(dvp_cam_ctlr_t *ctlr)
{
    if (NULL == ctlr) {
        return ESP_ERR_INVALID_ARG;
    }

    const uint32_t pin = (uint32_t)ctlr->vsync_pin;
    if (32U > pin) {
        gpio_ll_clear_intr_status(&GPIO, 1UL << pin);
    } else if (64U > pin) {
        gpio_ll_clear_intr_status_high(&GPIO, 1UL << (pin - 32U));
    } else {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t rc_dvp_staged_gpio_intr_enable(gpio_num_t gpio_num)
{
    dvp_cam_ctlr_t *const ctlr = s_rc_dvp_staged_ctlr;
    if (s_rc_dvp_quiesce_hold_requested &&
        (NULL != ctlr) && (gpio_num == ctlr->vsync_pin)) {
        const esp_err_t ret = gpio_intr_disable(gpio_num);
        if (ESP_OK == ret) {
            s_rc_dvp_quiesce_hold_acknowledged = true;
        }
        return ret;
    }
    return gpio_intr_enable(gpio_num);
}

static BaseType_t rc_dvp_staged_queue_receive(
    QueueHandle_t queue,
    void *item,
    TickType_t ticks_to_wait)
{
    const BaseType_t ret = xQueueReceive(queue, item, ticks_to_wait);
    const dvp_cam_event_t *event = (const dvp_cam_event_t *)item;
    dvp_cam_ctlr_t *const ctlr = s_rc_dvp_staged_ctlr;
    if ((pdPASS == ret) && (NULL != event) && (NULL != ctlr) &&
        (queue == ctlr->event_queue) &&
        (DVP_CAM_EVENT_SYNC_END == event->type) &&
        (DVP_CAM_FSM_RXING == ctlr->dvp_fsm)) {
        size_t received_blocks =
            (0U != ctlr->dma_buffer_hsize)
                ? (ctlr->trans.received_size / ctlr->dma_buffer_hsize)
                : 0U;
        const size_t hist_index =
            (received_blocks < (RC_DVP_STAGED_BLOCK_HIST_SIZE - 1U))
                ? received_blocks
                : (RC_DVP_STAGED_BLOCK_HIST_SIZE - 1U);
        s_rc_dvp_sync_end_received_blocks[hist_index]++;
        if (RC_DVP_STAGED_COPIED_BLOCKS_PER_FRAME == received_blocks) {
            s_rc_dvp_sync_end_complete_candidates++;
        } else {
            s_rc_dvp_sync_end_incomplete_events++;
            const int64_t last_control_us = s_rc_dvp_last_control_rx_us;
            const int64_t delta_us = esp_timer_get_time() - last_control_us;
            if ((0 < last_control_us) && (0 <= delta_us) &&
                (2000 >= delta_us)) {
                s_rc_dvp_incomplete_within_2ms++;
            } else if ((0 < last_control_us) && (0 <= delta_us) &&
                       (10000 >= delta_us)) {
                s_rc_dvp_incomplete_within_10ms++;
            } else {
                s_rc_dvp_incomplete_after_10ms++;
            }
        }
    }
    return ret;
}

static BaseType_t IRAM_ATTR rc_dvp_staged_queue_send_from_isr(
    QueueHandle_t queue,
    const void *item,
    BaseType_t *higher_priority_task_woken)
{
    const dvp_cam_event_t *event = (const dvp_cam_event_t *)item;
    dvp_cam_ctlr_t *const ctlr = s_rc_dvp_staged_ctlr;

    if ((NULL != event) && (DVP_CAM_EVENT_RECV_DATA == event->type)) {
        if ((NULL == ctlr) || (queue != ctlr->event_queue)) {
            return xQueueGenericSendFromISR(
                queue, item, higher_priority_task_woken,
                queueSEND_TO_BACK);
        }
        if ((DVP_CAM_FSM_RXING != ctlr->dvp_fsm) ||
            (NULL == ctlr->trans.buffer) ||
            (0U == ctlr->trans.buflen)) {
            return pdPASS;
        }

        const size_t block_size = ctlr->dma_buffer_hsize;
        const size_t next_size =
            ctlr->trans.received_size + block_size;
        if (next_size < ctlr->trans.buflen) {
            const size_t source_half = ctlr->dma_desc_index;
            const size_t block_index =
                (0U != block_size)
                    ? (ctlr->trans.received_size / block_size)
                    : 0U;
            const uintptr_t desc_addr =
                (uintptr_t)s_rc_dvp_last_eof_desc_addr;
            const uintptr_t desc_base = (uintptr_t)ctlr->dma_desc;
            const size_t desc_count =
                ctlr->dma_desc_hcnt * DVP_CAM_BUFFER_COUNT;
            const uintptr_t desc_end = desc_base +
                (desc_count * sizeof(dma_descriptor_t));
            ++s_rc_dvp_recv_data_events;
            if ((0U < ctlr->dma_desc_hcnt) &&
                (desc_addr >= desc_base) && (desc_addr < desc_end) &&
                (0U == ((desc_addr - desc_base) %
                        sizeof(dma_descriptor_t)))) {
                const size_t desc_index =
                    (desc_addr - desc_base) / sizeof(dma_descriptor_t);
                const size_t eof_half =
                    desc_index / ctlr->dma_desc_hcnt;
                if (eof_half == source_half) {
                    ++s_rc_dvp_eof_desc_matches_source;
                } else {
                    ++s_rc_dvp_eof_desc_mismatches_source;
                    const size_t mismatch_index =
                        (block_index <
                         (RC_DVP_STAGED_EOF_BLOCK_HIST_SIZE - 1U))
                            ? block_index
                            : (RC_DVP_STAGED_EOF_BLOCK_HIST_SIZE - 1U);
                    ++s_rc_dvp_eof_desc_mismatch_by_block[mismatch_index];
                }
            } else {
                ++s_rc_dvp_eof_desc_invalid;
            }
            memcpy(ctlr->trans.buffer + ctlr->trans.received_size,
                   &ctlr->dma_buffer[source_half * block_size],
                   block_size);
            ctlr->trans.received_size = next_size;
            ctlr->dma_desc_index =
                (source_half + 1U) % DVP_CAM_BUFFER_COUNT;
            return pdPASS;
        }
        if (next_size == ctlr->trans.buflen) {
            return pdPASS;
        }
        return pdPASS;
    }

    return xQueueGenericSendFromISR(
        queue, item, higher_priority_task_woken,
        queueSEND_TO_BACK);
}

esp_err_t rc_cam_new_dvp_ctlr_staged(
    const esp_cam_ctlr_dvp_config_t *config,
    esp_cam_ctlr_handle_t *ret_handle)
{
    const esp_err_t ret = rc_cam_new_dvp_ctlr_staged_impl(config, ret_handle);
    if ((ESP_OK == ret) && (NULL != ret_handle)) {
        s_rc_dvp_staged_ctlr = (dvp_cam_ctlr_t *)*ret_handle;
        rc_dvp_staged_reset_stats();
        ESP_LOGI(TAG,
                 "[STABLE-DVP] direct half-copy active dma=%u half=%u",
                 (unsigned)RC_DVP_STAGED_DMA_BUFFER_BYTES,
                 (unsigned)RC_DVP_STAGED_DMA_HALF_BYTES);
    }
    return ret;
}

esp_err_t rc_cam_del_dvp_ctlr_staged(esp_cam_ctlr_handle_t handle)
{
    if ((NULL == handle) ||
        ((esp_cam_ctlr_handle_t)s_rc_dvp_staged_ctlr != handle)) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_err_t ret = esp_cam_ctlr_del(handle);
    if (ESP_OK == ret) {
        s_rc_dvp_staged_ctlr = NULL;
        s_rc_dvp_quiesce_hold_requested = false;
        s_rc_dvp_quiesce_hold_acknowledged = false;
    }
    return ret;
}

esp_err_t rc_cam_ctlr_dvp_init_staged(
    int ctlr_id,
    cam_clock_source_t clk_src,
    const esp_cam_ctlr_dvp_pin_config_t *pin)
{
    return rc_cam_ctlr_dvp_init_staged_impl(ctlr_id, clk_src, pin);
}

esp_err_t rc_dvp_staged_prepare_resume(void)
{
    dvp_cam_ctlr_t *const ctlr = s_rc_dvp_staged_ctlr;
    if ((NULL == ctlr) || (NULL == ctlr->event_queue)) {
        return ESP_ERR_INVALID_STATE;
    }
    if (DVP_CAM_FSM_INIT != ctlr->dvp_fsm) {
        return ESP_ERR_INVALID_STATE;
    }

    if (pdPASS != xQueueReset(ctlr->event_queue)) {
        return ESP_FAIL;
    }
    const esp_err_t clear_ret = rc_dvp_staged_clear_pending_vsync(ctlr);
    if (ESP_OK != clear_ret) {
        return clear_ret;
    }

    s_rc_dvp_quiesce_hold_requested = false;
    s_rc_dvp_quiesce_hold_acknowledged = false;
    return ESP_OK;
}

void rc_dvp_staged_request_quiesce(void)
{
    s_rc_dvp_quiesce_hold_acknowledged = false;
    s_rc_dvp_quiesce_hold_requested = true;
}

esp_err_t rc_dvp_staged_wait_quiesced(uint32_t timeout_ms)
{
    if (0U == timeout_ms) {
        return ESP_ERR_INVALID_ARG;
    }

    const int64_t deadline_us = esp_timer_get_time() +
        ((int64_t)timeout_ms * 1000LL);
    while (!s_rc_dvp_quiesce_hold_acknowledged &&
           (esp_timer_get_time() < deadline_us)) {
        vTaskDelay(1U);
    }
    if (!s_rc_dvp_quiesce_hold_acknowledged) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

void rc_dvp_staged_get_stats(rc_dvp_staged_stats_t *stats)
{
    if (NULL == stats) {
        return;
    }
    stats->sync_end_complete_candidates =
        s_rc_dvp_sync_end_complete_candidates;
    stats->sync_end_incomplete_events =
        s_rc_dvp_sync_end_incomplete_events;
    for (size_t i = 0U; i < RC_DVP_STAGED_BLOCK_HIST_SIZE; ++i) {
        stats->sync_end_received_blocks[i] =
            s_rc_dvp_sync_end_received_blocks[i];
    }
    stats->sync_end_recovered_penultimate =
        s_rc_dvp_sync_end_recovered_penultimate;
    stats->recv_data_events = s_rc_dvp_recv_data_events;
    stats->eof_desc_matches_source =
        s_rc_dvp_eof_desc_matches_source;
    stats->eof_desc_mismatches_source =
        s_rc_dvp_eof_desc_mismatches_source;
    stats->eof_desc_invalid = s_rc_dvp_eof_desc_invalid;
    for (size_t i = 0U; i < RC_DVP_STAGED_EOF_BLOCK_HIST_SIZE; ++i) {
        stats->eof_desc_mismatch_by_block[i] =
            s_rc_dvp_eof_desc_mismatch_by_block[i];
    }
    stats->control_rx_events = s_rc_dvp_control_rx_events;
    stats->incomplete_within_2ms_of_control =
        s_rc_dvp_incomplete_within_2ms;
    stats->incomplete_within_10ms_of_control =
        s_rc_dvp_incomplete_within_10ms;
    stats->incomplete_after_10ms_of_control =
        s_rc_dvp_incomplete_after_10ms;
}

void rc_dvp_staged_note_control_rx(void)
{
    ++s_rc_dvp_control_rx_events;
    s_rc_dvp_last_control_rx_us = esp_timer_get_time();
}
#endif
