#include "companion_ui.h"

#include "board_laiwfs300.h"
#include "board_pins.h"
#include "companion_expression.h"
#include "companion_touch_gesture.h"
#include "display_hal.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"
#include "touch_hal.h"

#include <stddef.h>

#define COMPANION_UI_H_RES BOARD_LAIWFS300_LCD_V_RES
#define COMPANION_UI_V_RES BOARD_LAIWFS300_LCD_H_RES
#define COMPANION_UI_BUFFER_ROWS 16U
#define COMPANION_UI_TICK_MS 2U
#define COMPANION_UI_LOOP_MS 10U
#define COMPANION_UI_ANIMATION_MS 100U
#define COMPANION_UI_TASK_STACK 8192U
#define COMPANION_UI_TASK_PRIORITY 2U
#define COMPANION_UI_START_TIMEOUT_MS 5000U
#define COMPANION_UI_CANVAS_PIXELS \
    (COMPANION_UI_H_RES * COMPANION_UI_V_RES)
#define COMPANION_UI_CANVAS_BYTES \
    (COMPANION_UI_CANVAS_PIXELS * sizeof(uint16_t))
#define COMPANION_TOUCH_ERROR_LIMIT 3U
#define COMPANION_TOUCH_RECOVERY_LIMIT 3U

#if LV_COLOR_DEPTH != 16
#error "companion_ui requires LVGL RGB565 color depth"
#endif

static const char *TAG = "companion_ui";

static companion_ui_config_t s_config;
static companion_ui_state_t s_pending_state = {
    .product_state = COMPANION_PRODUCT_BOOTING,
    .roam_enabled = true,
};
static SemaphoreHandle_t s_state_lock;
static SemaphoreHandle_t s_start_terminal;
static volatile bool s_available;
static bool s_starting;
static bool s_started;
static bool s_fatal_reported;
static esp_err_t s_start_result = ESP_ERR_INVALID_STATE;
static portMUX_TYPE s_lifecycle_lock = portMUX_INITIALIZER_UNLOCKED;

static lv_obj_t *s_expression_canvas;
static lv_obj_t *s_state_label;
static lv_obj_t *s_roam_label;
static lv_obj_t *s_doa_label;
static lv_obj_t *s_net_label;

static void publish_touch_health(bool available, esp_err_t error)
{
    if (NULL != s_config.on_touch_health) {
        s_config.on_touch_health(available, error, s_config.user_ctx);
    }
}

static void report_fatal_error(const char *stage, esp_err_t error)
{
    bool publish = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    s_available = false;
    if (!s_fatal_reported) {
        s_fatal_reported = true;
        s_start_result = error;
        publish = true;
    }
    portEXIT_CRITICAL(&s_lifecycle_lock);
    if (publish) {
        publish_touch_health(false, error);
        ESP_LOGE(TAG, "%s failed: %s; UI capability disabled", stage,
                 esp_err_to_name(error));
        s_config.on_error(error, s_config.user_ctx);
        if (NULL != s_start_terminal) {
            (void)xSemaphoreGive(s_start_terminal);
        }
    }
}

static void mark_ui_task_stopped(void)
{
    portENTER_CRITICAL(&s_lifecycle_lock);
    s_available = false;
    s_started = false;
    portEXIT_CRITICAL(&s_lifecycle_lock);
}

static const char *state_name(companion_product_state_t state)
{
    switch (state) {
    case COMPANION_PRODUCT_BOOTING: return "BOOT";
    case COMPANION_PRODUCT_WAIT_NETWORK: return "IDLE";
    case COMPANION_PRODUCT_IDLE: return "IDLE";
    case COMPANION_PRODUCT_LOCATING: return "LOCATE";
    case COMPANION_PRODUCT_TURNING: return "TURN";
    case COMPANION_PRODUCT_CONNECTING: return "CONNECT";
    case COMPANION_PRODUCT_LISTENING: return "LISTEN";
    case COMPANION_PRODUCT_PROCESSING: return "THINK";
    case COMPANION_PRODUCT_SPEAKING: return "TALK";
    case COMPANION_PRODUCT_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

static void flush_cb(lv_disp_drv_t *driver, const lv_area_t *area,
                     lv_color_t *color_map)
{
    uint32_t pixel_count = (uint32_t)(area->x2 - area->x1 + 1) *
                           (uint32_t)(area->y2 - area->y1 + 1);
    uint16_t *pixels = (uint16_t *)color_map;
    for (uint32_t index = 0U; index < pixel_count; ++index) {
        const uint16_t value = pixels[index];
        const uint16_t red = (value >> 11) & 0x1FU;
        const uint16_t green = (value >> 5) & 0x3FU;
        const uint16_t blue = value & 0x1FU;
        const uint16_t bgr = (uint16_t)((blue << 11) | (green << 5) | red);
        pixels[index] = (uint16_t)((bgr >> 8) | (bgr << 8));
    }
    const esp_err_t result = display_hal_draw_bitmap_rgb565(
        area->x1, area->y1, area->x2 - area->x1 + 1,
        area->y2 - area->y1 + 1, pixels);
    lv_disp_flush_ready(driver);
    if (ESP_OK != result) {
        report_fatal_error("display flush", result);
    }
}

static void tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(COMPANION_UI_TICK_MS);
}

static bool ui_state_equal(const companion_ui_state_t *first,
                           const companion_ui_state_t *second)
{
    return first->product_state == second->product_state &&
           first->look_direction == second->look_direction &&
           first->roam_enabled == second->roam_enabled &&
           first->network_ready == second->network_ready &&
           first->doa_debug_valid == second->doa_debug_valid &&
           first->doa_remaining_deg == second->doa_remaining_deg &&
           first->generation == second->generation &&
           first->wake_seq == second->wake_seq;
}

static bool map_touch_coordinates(const touch_panel_point_t *raw_point,
                                  uint16_t *display_x,
                                  uint16_t *display_y)
{
    if (NULL == raw_point || NULL == display_x || NULL == display_y ||
        COMPANION_UI_V_RES <= raw_point->x ||
        COMPANION_UI_H_RES <= raw_point->y) {
        return false;
    }
    *display_x = (uint16_t)(COMPANION_UI_H_RES - 1U - raw_point->y);
    *display_y = raw_point->x;
    return true;
}

static esp_err_t create_ui(uint16_t *canvas_pixels)
{
    if (NULL == canvas_pixels) {
        return ESP_ERR_INVALID_ARG;
    }
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x05070A), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    s_expression_canvas = lv_canvas_create(screen);
    if (NULL == s_expression_canvas) {
        return ESP_ERR_NO_MEM;
    }
    lv_canvas_set_buffer(s_expression_canvas, canvas_pixels,
                         COMPANION_UI_H_RES, COMPANION_UI_V_RES,
                         LV_IMG_CF_TRUE_COLOR);
    lv_obj_set_pos(s_expression_canvas, 0, 0);
    lv_obj_clear_flag(s_expression_canvas, LV_OBJ_FLAG_SCROLLABLE);

    s_state_label = lv_label_create(screen);
    s_roam_label = lv_label_create(screen);
    s_doa_label = lv_label_create(screen);
    s_net_label = lv_label_create(screen);
    if (NULL == s_state_label || NULL == s_roam_label ||
        NULL == s_doa_label || NULL == s_net_label) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_style_text_color(s_state_label, lv_color_hex(0xF8FAFC), 0);
    lv_obj_set_style_text_color(s_roam_label, lv_color_hex(0x5EEAD4), 0);
    lv_obj_set_style_text_color(s_doa_label, lv_color_hex(0xFDE68A), 0);
    lv_obj_set_style_text_color(s_net_label, lv_color_hex(0x94A3B8), 0);
    lv_obj_align(s_state_label, LV_ALIGN_TOP_LEFT, 8, 8);
    lv_obj_align(s_roam_label, LV_ALIGN_TOP_RIGHT, -8, 8);
    lv_obj_align(s_doa_label, LV_ALIGN_BOTTOM_LEFT, 8, -8);
    lv_obj_align(s_net_label, LV_ALIGN_BOTTOM_RIGHT, -8, -8);
    return ESP_OK;
}

static void update_status_labels(const companion_ui_state_t *ui_state)
{
    lv_label_set_text_fmt(s_state_label, "%s G%lu W%lu",
                          state_name(ui_state->product_state),
                          (unsigned long)ui_state->generation,
                          (unsigned long)ui_state->wake_seq);
    lv_label_set_text(s_roam_label, ui_state->roam_enabled ? "ROAM ON" : "ROAM OFF");
    if (!ui_state->doa_debug_valid) {
        lv_label_set_text(s_doa_label, "DOA --");
    } else if (0 < ui_state->doa_remaining_deg) {
        lv_label_set_text_fmt(s_doa_label, "DOA L%d",
                              (int)ui_state->doa_remaining_deg);
    } else if (0 > ui_state->doa_remaining_deg) {
        lv_label_set_text_fmt(s_doa_label, "DOA R%d",
                              -(int)ui_state->doa_remaining_deg);
    } else {
        lv_label_set_text(s_doa_label, "DOA 0");
    }
    lv_label_set_text(s_net_label, ui_state->network_ready ? "4G READY" : "4G WAIT");
}

static void ui_task(void *arg)
{
    (void)arg;
    esp_err_t result = board_laiwfs300_display_init();
    if (ESP_OK == result) {
        result = display_hal_set_orientation(true, false, true);
    }
    if (ESP_OK != result) {
        report_fatal_error("display init", result);
        mark_ui_task_stopped();
        vTaskDelete(NULL);
        return;
    }

    const esp_err_t touch_init_result = board_laiwfs300_touch_init();
    bool touch_available = (ESP_OK == touch_init_result);
    if (!touch_available) {
        ESP_LOGW(TAG, "touch unavailable: %s; display remains active",
                 esp_err_to_name(touch_init_result));
        publish_touch_health(false, touch_init_result);
    }

    lv_init();
    const size_t buffer_pixels = COMPANION_UI_H_RES * COMPANION_UI_BUFFER_ROWS;
    lv_color_t *buffer1 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t),
                                           MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    lv_color_t *buffer2 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t),
                                           MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == buffer1 || NULL == buffer2) {
        heap_caps_free(buffer1);
        heap_caps_free(buffer2);
        report_fatal_error("LVGL draw buffer allocation", ESP_ERR_NO_MEM);
        mark_ui_task_stopped();
        vTaskDelete(NULL);
        return;
    }

    static lv_disp_draw_buf_t draw_buffer;
    static lv_disp_drv_t display_driver;
    lv_disp_draw_buf_init(&draw_buffer, buffer1, buffer2, buffer_pixels);
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = COMPANION_UI_H_RES;
    display_driver.ver_res = COMPANION_UI_V_RES;
    display_driver.flush_cb = flush_cb;
    display_driver.draw_buf = &draw_buffer;
    lv_disp_drv_register(&display_driver);

    companion_expression_config_t expression_config = {0};
    companion_expression_config_default(&expression_config);
    companion_expression_t *expression = NULL;
    result = companion_expression_open(&expression_config, &expression);
    if (ESP_OK != result) {
        heap_caps_free(buffer1);
        heap_caps_free(buffer2);
        report_fatal_error("expression catalog", result);
        mark_ui_task_stopped();
        vTaskDelete(NULL);
        return;
    }

    const size_t psram_before = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    uint16_t *canvas_pixels = heap_caps_malloc(
        COMPANION_UI_CANVAS_BYTES, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    const size_t psram_after = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    if (NULL == canvas_pixels) {
        companion_expression_close(expression);
        heap_caps_free(buffer1);
        heap_caps_free(buffer2);
        ESP_LOGE(TAG, "canvas allocation failed bytes=%u psram_free=%u",
                 (unsigned)COMPANION_UI_CANVAS_BYTES,
                 (unsigned)psram_before);
        report_fatal_error("expression canvas allocation", ESP_ERR_NO_MEM);
        mark_ui_task_stopped();
        vTaskDelete(NULL);
        return;
    }
    ESP_LOGI(TAG, "canvas allocated bytes=%u psram_before=%u psram_after=%u",
             (unsigned)COMPANION_UI_CANVAS_BYTES, (unsigned)psram_before,
             (unsigned)psram_after);

    companion_ui_state_t current_state = {
        .product_state = COMPANION_PRODUCT_BOOTING,
        .roam_enabled = true,
    };
    if (NULL != s_state_lock &&
        pdTRUE == xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(5))) {
        current_state = s_pending_state;
        xSemaphoreGive(s_state_lock);
    }
    companion_expression_signals_t expression_signals = {
        .product_state = current_state.product_state,
        .turn_direction = current_state.look_direction,
        .touch_active = false,
    };
    companion_rgb565_surface_t expression_surface = {
        .pixels = canvas_pixels,
        .width = COMPANION_UI_H_RES,
        .height = COMPANION_UI_V_RES,
        .stride_pixels = COMPANION_UI_H_RES,
    };
    companion_expression_result_t expression_result = {0};
    uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
    result = companion_expression_render(expression, &expression_signals,
                                         now_ms, esp_random(),
                                         &expression_surface,
                                         &expression_result);
    if (ESP_OK == result) {
        result = create_ui(canvas_pixels);
    }
    if (ESP_OK != result) {
        companion_expression_close(expression);
        heap_caps_free(canvas_pixels);
        heap_caps_free(buffer1);
        heap_caps_free(buffer2);
        report_fatal_error("initial expression render", result);
        mark_ui_task_stopped();
        vTaskDelete(NULL);
        return;
    }
    update_status_labels(&current_state);

    companion_touch_gesture_t touch_gesture = {0};
    const companion_touch_gesture_config_t gesture_config = {
        .display_width = COMPANION_UI_H_RES,
        .display_height = COMPANION_UI_V_RES,
        .press_debounce_ms = s_config.touch_press_debounce_ms,
        .touch_decision_ms = s_config.touch_decision_ms,
        .release_debounce_ms = s_config.touch_release_debounce_ms,
        .tap_feedback_ms = s_config.touch_tap_feedback_ms,
        .swipe_intent_horizontal_px =
            s_config.swipe_intent_horizontal_px,
        .swipe_min_horizontal_px = s_config.swipe_min_horizontal_px,
        .swipe_max_vertical_px = s_config.swipe_max_vertical_px,
        .swipe_max_duration_ms = s_config.swipe_max_duration_ms,
    };
    if (touch_available) {
        result = companion_touch_gesture_init(&touch_gesture,
                                              &gesture_config);
        if (ESP_OK != result) {
            touch_available = false;
            ESP_LOGE(TAG, "touch gesture init failed: %s; touch disabled",
                     esp_err_to_name(result));
            publish_touch_health(false, result);
        } else {
            publish_touch_health(true, ESP_OK);
            ESP_LOGI(TAG,
                     "gesture ready press=%ums decision=%ums release=%ums tap_feedback=%ums swipe_intent=%upx swipe_switch=%upx swipe_v=%upx max=%ums map=(319-raw_y,raw_x)",
                     (unsigned)gesture_config.press_debounce_ms,
                     (unsigned)gesture_config.touch_decision_ms,
                     (unsigned)gesture_config.release_debounce_ms,
                     (unsigned)gesture_config.tap_feedback_ms,
                     (unsigned)gesture_config.swipe_intent_horizontal_px,
                     (unsigned)gesture_config.swipe_min_horizontal_px,
                     (unsigned)gesture_config.swipe_max_vertical_px,
                     (unsigned)gesture_config.swipe_max_duration_ms);
        }
    }

    const esp_timer_create_args_t tick_args = {
        .callback = tick_cb,
        .name = "companion_lv_tick",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t tick_timer = NULL;
    result = esp_timer_create(&tick_args, &tick_timer);
    if (ESP_OK == result) {
        result = esp_timer_start_periodic(tick_timer,
                                          COMPANION_UI_TICK_MS * 1000U);
    }
    if (ESP_OK != result) {
        if (NULL != tick_timer) {
            (void)esp_timer_delete(tick_timer);
        }
        companion_expression_close(expression);
        heap_caps_free(canvas_pixels);
        heap_caps_free(buffer1);
        heap_caps_free(buffer2);
        report_fatal_error("LVGL tick start", result);
        mark_ui_task_stopped();
        vTaskDelete(NULL);
        return;
    }

    uint64_t last_animation_ms = now_ms;
    esp_err_t last_touch_read_error = ESP_OK;
    esp_err_t last_gesture_error = ESP_OK;
    esp_err_t last_render_error = ESP_OK;
    uint32_t consecutive_touch_errors = 0U;
    uint32_t consecutive_touch_successes = 0U;
    bool touch_health_available = touch_available;
    uint16_t last_display_x = 0U;
    uint16_t last_display_y = 0U;
    bool invalid_coordinate_active = false;
    bool render_requested = false;
    bool startup_cancelled = false;
    portENTER_CRITICAL(&s_lifecycle_lock);
    startup_cancelled = s_fatal_reported;
    if (!startup_cancelled) {
        s_available = true;
        s_start_result = ESP_OK;
    }
    portEXIT_CRITICAL(&s_lifecycle_lock);
    if (startup_cancelled) {
        goto ui_task_cleanup;
    }
    if (NULL != s_start_terminal) {
        (void)xSemaphoreGive(s_start_terminal);
    }
    ESP_LOGI(TAG,
             "expression UI ready touch=%s packs=%u initial_pack=%s canvas=320x240 draw_rows=%u",
             touch_available ? "ready" : "unavailable",
             (unsigned)companion_expression_pack_count(expression),
             expression_result.current_pack_id,
             COMPANION_UI_BUFFER_ROWS);
    while (s_available) {
        now_ms = (uint64_t)(esp_timer_get_time() / 1000ULL);
        if (NULL != s_state_lock &&
            pdTRUE == xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(5))) {
            if (!ui_state_equal(&current_state, &s_pending_state)) {
                current_state = s_pending_state;
                expression_signals.product_state = current_state.product_state;
                expression_signals.turn_direction =
                    current_state.look_direction;
                update_status_labels(&current_state);
                render_requested = true;
            }
            xSemaphoreGive(s_state_lock);
        }

        if (touch_available) {
            touch_panel_point_t raw_point = {0};
            uint8_t count = 0U;
            const esp_err_t touch_read_result = touch_panel_read_point(
                &raw_point, &count);
            bool raw_touch = (ESP_OK == touch_read_result &&
                              0U < count && 2U >= count);
            if (ESP_OK != touch_read_result) {
                consecutive_touch_errors++;
                consecutive_touch_successes = 0U;
                if (last_touch_read_error != touch_read_result) {
                    ESP_LOGW(TAG, "touch read failed: %s",
                             esp_err_to_name(touch_read_result));
                }
                last_touch_read_error = touch_read_result;
                if (touch_health_available &&
                    COMPANION_TOUCH_ERROR_LIMIT <=
                        consecutive_touch_errors) {
                    touch_health_available = false;
                    (void)companion_touch_gesture_init(&touch_gesture,
                                                       &gesture_config);
                    if (expression_signals.touch_active) {
                        expression_signals.touch_active = false;
                        render_requested = true;
                        s_config.on_touch(false, s_config.user_ctx);
                    }
                    publish_touch_health(false, touch_read_result);
                    ESP_LOGE(TAG,
                             "touch capability down after %u read failures",
                             COMPANION_TOUCH_ERROR_LIMIT);
                }
                goto touch_poll_complete;
            } else {
                consecutive_touch_errors = 0U;
                if (!touch_health_available) {
                    consecutive_touch_successes++;
                    if (COMPANION_TOUCH_RECOVERY_LIMIT <=
                        consecutive_touch_successes) {
                        const esp_err_t reset_result =
                            companion_touch_gesture_init(&touch_gesture,
                                                         &gesture_config);
                        if (ESP_OK == reset_result) {
                            touch_health_available = true;
                            consecutive_touch_successes = 0U;
                            last_touch_read_error = ESP_OK;
                            publish_touch_health(true, ESP_OK);
                            ESP_LOGI(TAG,
                                     "touch capability recovered after %u reads",
                                     COMPANION_TOUCH_RECOVERY_LIMIT);
                        }
                    }
                    goto touch_poll_complete;
                }
                consecutive_touch_successes = 0U;
                last_touch_read_error = ESP_OK;
            }

            uint16_t display_x = last_display_x;
            uint16_t display_y = last_display_y;
            if (raw_touch) {
                if (map_touch_coordinates(&raw_point, &display_x,
                                          &display_y)) {
                    last_display_x = display_x;
                    last_display_y = display_y;
                    invalid_coordinate_active = false;
                } else {
                    if (!invalid_coordinate_active) {
                        ESP_LOGW(TAG,
                                 "touch coordinate rejected raw=(%u,%u) expected raw_x<240 raw_y<320",
                                 raw_point.x, raw_point.y);
                    }
                    invalid_coordinate_active = true;
                    raw_touch = false;
                }
            } else {
                invalid_coordinate_active = false;
            }

            companion_touch_gesture_result_t gesture_result = {0};
            const esp_err_t gesture_error = companion_touch_gesture_update(
                &touch_gesture, raw_touch, display_x, display_y, now_ms,
                &gesture_result);
            if (ESP_OK != gesture_error) {
                if (last_gesture_error != gesture_error) {
                    ESP_LOGE(TAG, "gesture update failed: %s",
                             esp_err_to_name(gesture_error));
                }
                last_gesture_error = gesture_error;
            } else {
                if (ESP_OK != last_gesture_error) {
                    ESP_LOGI(TAG, "gesture processing recovered");
                }
                last_gesture_error = ESP_OK;

                if (COMPANION_TOUCH_TRANSITION_PRESSED ==
                    gesture_result.touch_transition) {
                    expression_signals.touch_active = true;
                    render_requested = true;
                    ESP_LOGI(TAG,
                             "touch pressed source=%s display=(%u,%u) contact=%ums",
                             gesture_result.synthetic_feedback ?
                                 "quick_tap" : "hold",
                             display_x, display_y,
                             (unsigned)gesture_result.duration_ms);
                    s_config.on_touch(true, s_config.user_ctx);
                } else if (COMPANION_TOUCH_TRANSITION_RELEASED ==
                           gesture_result.touch_transition) {
                    expression_signals.touch_active = false;
                    render_requested = true;
                    ESP_LOGI(TAG,
                             "touch released source=%s display=(%u,%u) duration=%ums release_debounce=%ums",
                             gesture_result.synthetic_feedback ?
                                 "quick_tap" : "hold",
                             last_display_x, last_display_y,
                             (unsigned)gesture_result.duration_ms,
                             (unsigned)s_config.touch_release_debounce_ms);
                    s_config.on_touch(false, s_config.user_ctx);
                }

                if (COMPANION_GESTURE_PACK_NONE !=
                    gesture_result.pack_step) {
                    const bool next_pack =
                        (COMPANION_GESTURE_PACK_NEXT ==
                         gesture_result.pack_step);
                    const companion_pack_step_t pack_step = next_pack ?
                        COMPANION_PACK_NEXT : COMPANION_PACK_PREVIOUS;
                    ESP_LOGI(TAG,
                             "swipe direction=%s action=%s dx=%d dy=%d duration=%ums",
                             (0 > gesture_result.delta_x) ? "left" : "right",
                             next_pack ? "next" : "previous",
                             (int)gesture_result.delta_x,
                             (int)gesture_result.delta_y,
                             (unsigned)gesture_result.duration_ms);
                    const esp_err_t pack_result =
                        companion_expression_step_pack(expression, pack_step);
                    if (ESP_OK != pack_result) {
                        ESP_LOGE(TAG, "pack switch failed: %s",
                                 esp_err_to_name(pack_result));
                    } else {
                        render_requested = true;
                    }
                }
            }
        }

touch_poll_complete:
        if (render_requested ||
            (now_ms - last_animation_ms) >= COMPANION_UI_ANIMATION_MS) {
            last_animation_ms = now_ms;
            expression_result = (companion_expression_result_t) {0};
            const esp_err_t render_error = companion_expression_render(
                expression, &expression_signals, now_ms, esp_random(),
                &expression_surface, &expression_result);
            if (ESP_OK == render_error) {
                if (ESP_OK != last_render_error) {
                    ESP_LOGI(TAG, "expression render recovered pack=%s",
                             expression_result.current_pack_id);
                }
                last_render_error = ESP_OK;
                if (expression_result.changed) {
                    lv_obj_invalidate(s_expression_canvas);
                }
            } else {
                if (last_render_error != render_error) {
                    ESP_LOGE(TAG, "expression render failed: %s; canvas cleared",
                             esp_err_to_name(render_error));
                    lv_obj_invalidate(s_expression_canvas);
                }
                last_render_error = render_error;
            }
            render_requested = false;
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(COMPANION_UI_LOOP_MS));
    }
ui_task_cleanup:
    (void)esp_timer_stop(tick_timer);
    (void)esp_timer_delete(tick_timer);
    companion_expression_close(expression);
    heap_caps_free(canvas_pixels);
    heap_caps_free(buffer1);
    heap_caps_free(buffer2);
    mark_ui_task_stopped();
    vTaskDelete(NULL);
}

esp_err_t companion_ui_start(const companion_ui_config_t *config)
{
    if (NULL == config || NULL == config->on_touch ||
        NULL == config->on_error || NULL == config->on_touch_health ||
        0U == config->touch_press_debounce_ms ||
        config->touch_decision_ms < config->touch_press_debounce_ms ||
        0U == config->touch_release_debounce_ms ||
        0U == config->touch_tap_feedback_ms ||
        0U == config->swipe_intent_horizontal_px ||
        0U == config->swipe_min_horizontal_px ||
        config->swipe_min_horizontal_px <=
            config->swipe_intent_horizontal_px ||
        COMPANION_UI_H_RES <= config->swipe_min_horizontal_px ||
        COMPANION_UI_V_RES <= config->swipe_max_vertical_px ||
        config->swipe_max_duration_ms < config->touch_decision_ms) {
        return ESP_ERR_INVALID_ARG;
    }
    portENTER_CRITICAL(&s_lifecycle_lock);
    if (s_fatal_reported) {
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_ERR_INVALID_STATE;
    }
    if (s_started && !s_starting) {
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_OK;
    }
    if (s_starting) {
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_ERR_INVALID_STATE;
    }
    s_starting = true;
    s_started = true;
    s_available = false;
    s_fatal_reported = false;
    s_start_result = ESP_ERR_INVALID_STATE;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    s_config = *config;
    if (NULL == s_start_terminal) {
        s_start_terminal = xSemaphoreCreateBinary();
    }
    if (NULL == s_start_terminal) {
        portENTER_CRITICAL(&s_lifecycle_lock);
        s_starting = false;
        s_started = false;
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_ERR_NO_MEM;
    }
    while (pdTRUE == xSemaphoreTake(s_start_terminal, 0U)) {
    }
    s_state_lock = xSemaphoreCreateMutex();
    if (NULL == s_state_lock) {
        portENTER_CRITICAL(&s_lifecycle_lock);
        s_starting = false;
        s_started = false;
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_ERR_NO_MEM;
    }
    BaseType_t result = xTaskCreatePinnedToCore(
        ui_task, "companion_ui", COMPANION_UI_TASK_STACK, NULL,
        COMPANION_UI_TASK_PRIORITY, NULL, 1);
    if (pdPASS != result) {
        vSemaphoreDelete(s_state_lock);
        s_state_lock = NULL;
        portENTER_CRITICAL(&s_lifecycle_lock);
        s_starting = false;
        s_started = false;
        portEXIT_CRITICAL(&s_lifecycle_lock);
        return ESP_FAIL;
    }
    if (pdTRUE != xSemaphoreTake(
            s_start_terminal,
            pdMS_TO_TICKS(COMPANION_UI_START_TIMEOUT_MS))) {
        report_fatal_error("UI start acknowledgement", ESP_ERR_TIMEOUT);
    }
    portENTER_CRITICAL(&s_lifecycle_lock);
    const esp_err_t start_result = s_start_result;
    s_starting = false;
    portEXIT_CRITICAL(&s_lifecycle_lock);
    return start_result;
}

esp_err_t companion_ui_set_state(const companion_ui_state_t *state)
{
    if (NULL == state || COMPANION_PRODUCT_STATE_COUNT <= state->product_state ||
        NULL == s_state_lock) {
        return ESP_ERR_INVALID_ARG;
    }
    if (pdTRUE != xSemaphoreTake(s_state_lock, pdMS_TO_TICKS(20))) {
        return ESP_ERR_TIMEOUT;
    }
    s_pending_state = *state;
    xSemaphoreGive(s_state_lock);
    return ESP_OK;
}

bool companion_ui_is_available(void)
{
    return s_available;
}
