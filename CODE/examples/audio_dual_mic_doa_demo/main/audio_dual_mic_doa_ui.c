#include "audio_dual_mic_doa_ui.h"

#include "board_laiwfs300.h"
#include "board_pins.h"
#include "display_hal.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <math.h>
#include <stdio.h>

static const char *TAG = "audio_dual_mic_doa_ui";

#define UI_HOR_RES             BOARD_LAIWFS300_LCD_V_RES
#define UI_VER_RES             BOARD_LAIWFS300_LCD_H_RES
#define UI_BUFFER_ROWS         BOARD_LAIWFS300_LCD_DRAW_BUF_LINES
#define UI_TICK_MS             2
#define UI_TASK_DELAY_MS       10
#define UI_TASK_STACK          8192
#define UI_TASK_PRIORITY       2
#define UI_TRACK_X             50
#define UI_TRACK_Y             108
#define UI_TRACK_W             220
#define UI_TRACK_H             4
#define UI_INDICATOR_SIZE      18
#define UI_MIC_BAR_X           88
#define UI_MIC_BAR_W           188
#define UI_MIC_BAR_H           10
#define UI_MIC_BAR_MAX_RMS     2000
#define UI_FIRST_FRAME_TIMEOUT_MS 200

static SemaphoreHandle_t s_lvgl_mutex;
static lv_color_t *s_lvgl_buf1;
static lv_color_t *s_lvgl_buf2;
static esp_timer_handle_t s_lvgl_tick_timer;
static bool s_ui_ready;

static lv_obj_t *s_direction_label;
static lv_obj_t *s_angle_label;
static lv_obj_t *s_energy_label;
static lv_obj_t *s_status_label;
static lv_obj_t *s_indicator;
static lv_obj_t *s_mic1_bar;
static lv_obj_t *s_mic2_bar;
static lv_obj_t *s_mic1_value_label;
static lv_obj_t *s_mic2_value_label;

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    uint32_t pixel_count = (uint32_t)(area->x2 - area->x1 + 1) *
                           (uint32_t)(area->y2 - area->y1 + 1);
    uint16_t *px = (uint16_t *)color_map;
    for (uint32_t i = 0; i < pixel_count; i++) {
        uint16_t p = px[i];
        uint16_t r = (p >> 11) & 0x1F;
        uint16_t g = (p >> 5) & 0x3F;
        uint16_t b = p & 0x1F;
        uint16_t bgr = (b << 11) | (g << 5) | r;
        px[i] = (uint16_t)((bgr >> 8) | (bgr << 8));
    }

    display_hal_draw_bitmap_rgb565(area->x1,
                                   area->y1,
                                   area->x2 - area->x1 + 1,
                                   area->y2 - area->y1 + 1,
                                   px);
    lv_disp_flush_ready(drv);
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(UI_TICK_MS);
}

static void lvgl_task(void *arg)
{
    (void)arg;

    while (true) {
        xSemaphoreTakeRecursive(s_lvgl_mutex, portMAX_DELAY);
        lv_timer_handler();
        xSemaphoreGiveRecursive(s_lvgl_mutex);
        vTaskDelay(pdMS_TO_TICKS(UI_TASK_DELAY_MS));
    }
}

static lv_color_t ui_direction_color(bool active, doa_ui_direction_t direction)
{
    if (!active || DOA_UI_DIRECTION_IDLE == direction) {
        return lv_color_make(130, 140, 150);
    }

    switch (direction) {
    case DOA_UI_DIRECTION_LEFT:
        return lv_color_make(64, 196, 255);
    case DOA_UI_DIRECTION_CENTER:
        return lv_color_make(88, 214, 141);
    case DOA_UI_DIRECTION_RIGHT:
        return lv_color_make(255, 179, 71);
    case DOA_UI_DIRECTION_IDLE:
    default:
        return lv_color_make(130, 140, 150);
    }
}

static const char *ui_direction_text(bool active, doa_ui_direction_t direction)
{
    if (!active || DOA_UI_DIRECTION_IDLE == direction) {
        return "IDLE";
    }

    switch (direction) {
    case DOA_UI_DIRECTION_LEFT:
        return "LEFT";
    case DOA_UI_DIRECTION_CENTER:
        return "CENTER";
    case DOA_UI_DIRECTION_RIGHT:
        return "RIGHT";
    case DOA_UI_DIRECTION_IDLE:
    default:
        return "IDLE";
    }
}

static int ui_indicator_x_from_relative(float relative_deg)
{
    float clamped = relative_deg;
    if (clamped < -90.0f) {
        clamped = -90.0f;
    }
    if (clamped > 90.0f) {
        clamped = 90.0f;
    }

    float normalized = (clamped + 90.0f) / 180.0f;
    int travel = UI_TRACK_W - UI_INDICATOR_SIZE;
    return UI_TRACK_X + (int)lroundf(normalized * (float)travel);
}

static int ui_bar_value_from_rms(uint32_t rms)
{
    if (rms >= UI_MIC_BAR_MAX_RMS) {
        return UI_MIC_BAR_MAX_RMS;
    }
    return (int)rms;
}

static void ui_create_track_marker(lv_obj_t *parent, int x, const char *text)
{
    lv_obj_t *tick = lv_obj_create(parent);
    lv_obj_remove_style_all(tick);
    lv_obj_set_size(tick, 2, 12);
    lv_obj_set_style_bg_color(tick, lv_color_make(90, 105, 120), 0);
    lv_obj_set_style_bg_opa(tick, LV_OPA_COVER, 0);
    lv_obj_set_pos(tick, x, UI_TRACK_Y - 4);

    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_make(150, 165, 180), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(label, x - 18, UI_TRACK_Y + 14);
    lv_obj_set_width(label, 36);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
}

static esp_err_t ui_init_screen(void)
{
    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_make(10, 14, 20), 0);
    lv_obj_set_style_bg_opa(scr, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(scr);
    lv_label_set_text(title, "DUAL MIC DOA");
    lv_obj_set_style_text_color(title, lv_color_make(180, 190, 205), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);
    lv_obj_set_width(title, UI_HOR_RES);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(title, 0, 12);

    s_direction_label = lv_label_create(scr);
    lv_label_set_text(s_direction_label, "IDLE");
    lv_obj_set_style_text_color(s_direction_label, lv_color_make(130, 140, 150), 0);
    lv_obj_set_style_text_font(s_direction_label, &lv_font_montserrat_32, 0);
    lv_obj_set_width(s_direction_label, UI_HOR_RES);
    lv_obj_set_style_text_align(s_direction_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_direction_label, 0, 42);

    s_angle_label = lv_label_create(scr);
    lv_label_set_text(s_angle_label, "DOA --   REL --");
    lv_obj_set_style_text_color(s_angle_label, lv_color_make(210, 218, 230), 0);
    lv_obj_set_style_text_font(s_angle_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_angle_label, UI_HOR_RES);
    lv_obj_set_style_text_align(s_angle_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_angle_label, 0, 80);

    lv_obj_t *track = lv_obj_create(scr);
    lv_obj_remove_style_all(track);
    lv_obj_set_size(track, UI_TRACK_W, UI_TRACK_H);
    lv_obj_set_style_bg_color(track, lv_color_make(45, 60, 75), 0);
    lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(track, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_pos(track, UI_TRACK_X, UI_TRACK_Y);

    ui_create_track_marker(scr, UI_TRACK_X, "L");
    ui_create_track_marker(scr, UI_TRACK_X + UI_TRACK_W / 2, "C");
    ui_create_track_marker(scr, UI_TRACK_X + UI_TRACK_W, "R");

    s_indicator = lv_obj_create(scr);
    lv_obj_remove_style_all(s_indicator);
    lv_obj_set_size(s_indicator, UI_INDICATOR_SIZE, UI_INDICATOR_SIZE);
    lv_obj_set_style_radius(s_indicator, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_indicator, lv_color_make(130, 140, 150), 0);
    lv_obj_set_style_bg_opa(s_indicator, LV_OPA_COVER, 0);
    lv_obj_set_pos(s_indicator,
                   ui_indicator_x_from_relative(0.0f),
                   UI_TRACK_Y - ((UI_INDICATOR_SIZE - UI_TRACK_H) / 2));

    s_energy_label = lv_label_create(scr);
    lv_label_set_text(s_energy_label, "E -- dB");
    lv_obj_set_style_text_color(s_energy_label, lv_color_make(160, 170, 185), 0);
    lv_obj_set_style_text_font(s_energy_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_energy_label, UI_HOR_RES);
    lv_obj_set_style_text_align(s_energy_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_energy_label, 0, 146);

    s_status_label = lv_label_create(scr);
    lv_label_set_text(s_status_label, "Waiting for voice");
    lv_obj_set_style_text_color(s_status_label, lv_color_make(130, 140, 150), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_status_label, UI_HOR_RES);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_status_label, 0, 168);

    lv_obj_t *mic1_label = lv_label_create(scr);
    lv_label_set_text(mic1_label, "MIC1");
    lv_obj_set_style_text_color(mic1_label, lv_color_make(170, 180, 195), 0);
    lv_obj_set_style_text_font(mic1_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(mic1_label, 28, 194);

    s_mic1_bar = lv_bar_create(scr);
    lv_obj_set_size(s_mic1_bar, UI_MIC_BAR_W, UI_MIC_BAR_H);
    lv_obj_set_pos(s_mic1_bar, UI_MIC_BAR_X, 198);
    lv_bar_set_range(s_mic1_bar, 0, UI_MIC_BAR_MAX_RMS);
    lv_obj_set_style_bg_color(s_mic1_bar, lv_color_make(38, 48, 60), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_mic1_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_mic1_bar, lv_color_make(64, 196, 255), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_mic1_bar, LV_OPA_COVER, LV_PART_INDICATOR);

    s_mic1_value_label = lv_label_create(scr);
    lv_label_set_text(s_mic1_value_label, "0");
    lv_obj_set_style_text_color(s_mic1_value_label, lv_color_make(170, 180, 195), 0);
    lv_obj_set_style_text_font(s_mic1_value_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_mic1_value_label, 282, 190);
    lv_obj_set_width(s_mic1_value_label, 34);
    lv_obj_set_style_text_align(s_mic1_value_label, LV_TEXT_ALIGN_RIGHT, 0);

    lv_obj_t *mic2_label = lv_label_create(scr);
    lv_label_set_text(mic2_label, "MIC2");
    lv_obj_set_style_text_color(mic2_label, lv_color_make(170, 180, 195), 0);
    lv_obj_set_style_text_font(mic2_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(mic2_label, 28, 214);

    s_mic2_bar = lv_bar_create(scr);
    lv_obj_set_size(s_mic2_bar, UI_MIC_BAR_W, UI_MIC_BAR_H);
    lv_obj_set_pos(s_mic2_bar, UI_MIC_BAR_X, 218);
    lv_bar_set_range(s_mic2_bar, 0, UI_MIC_BAR_MAX_RMS);
    lv_obj_set_style_bg_color(s_mic2_bar, lv_color_make(38, 48, 60), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_mic2_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_mic2_bar, lv_color_make(255, 179, 71), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_mic2_bar, LV_OPA_COVER, LV_PART_INDICATOR);

    s_mic2_value_label = lv_label_create(scr);
    lv_label_set_text(s_mic2_value_label, "0");
    lv_obj_set_style_text_color(s_mic2_value_label, lv_color_make(170, 180, 195), 0);
    lv_obj_set_style_text_font(s_mic2_value_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_mic2_value_label, 282, 210);
    lv_obj_set_width(s_mic2_value_label, 34);
    lv_obj_set_style_text_align(s_mic2_value_label, LV_TEXT_ALIGN_RIGHT, 0);

    return ESP_OK;
}

static esp_err_t ui_present_first_frame(void)
{
    lv_timer_handler();
    return display_hal_wait_pending(UI_FIRST_FRAME_TIMEOUT_MS);
}

esp_err_t audio_dual_mic_doa_ui_init(void)
{
    if (s_ui_ready) {
        return ESP_OK;
    }

    esp_err_t ret = board_laiwfs300_display_init();
    if (ESP_OK != ret) {
        return ret;
    }

    ret = display_hal_set_orientation(true, false, true);
    if (ESP_OK != ret) {
        return ret;
    }

    /* 先做一次纯色填充，确保背光和面板在进入 LVGL 前已经有可见反馈。 */
    ret = board_laiwfs300_display_fill_rgb565(DISPLAY_HAL_RGB565_WHITE);
    if (ESP_OK != ret) {
        return ret;
    }

    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (NULL == s_lvgl_mutex) {
        return ESP_ERR_NO_MEM;
    }

    lv_init();

    const size_t buf_pixels = UI_HOR_RES * UI_BUFFER_ROWS;
    s_lvgl_buf1 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    s_lvgl_buf2 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == s_lvgl_buf1 || NULL == s_lvgl_buf2) {
        ESP_LOGE(TAG, "LVGL draw buffer alloc failed");
        return ESP_ERR_NO_MEM;
    }

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, s_lvgl_buf1, s_lvgl_buf2, buf_pixels);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = UI_HOR_RES;
    disp_drv.ver_res = UI_VER_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    ESP_RETURN_ON_ERROR(ui_init_screen(), TAG, "init screen");
    ESP_RETURN_ON_ERROR(ui_present_first_frame(), TAG, "present first frame");

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "doa_lvgl_tick",
        .skip_unhandled_events = true,
    };
    ret = esp_timer_create(&tick_args, &s_lvgl_tick_timer);
    if (ESP_OK != ret) {
        return ret;
    }
    ret = esp_timer_start_periodic(s_lvgl_tick_timer, UI_TICK_MS * 1000U);
    if (ESP_OK != ret) {
        return ret;
    }

    BaseType_t ok = xTaskCreate(lvgl_task, "doa_lvgl", UI_TASK_STACK, NULL, UI_TASK_PRIORITY, NULL);
    if (pdPASS != ok) {
        return ESP_ERR_NO_MEM;
    }

    s_ui_ready = true;
    ESP_LOGI(TAG, "DOA UI initialized: %dx%d landscape", UI_HOR_RES, UI_VER_RES);
    return ESP_OK;
}

esp_err_t audio_dual_mic_doa_ui_update(const doa_ui_state_t *state)
{
    char angle_text[48];
    char energy_text[32];
    char mic1_text[16];
    char mic2_text[16];
    lv_color_t direction_color;
    int indicator_x;

    if (!s_ui_ready || NULL == state) {
        return ESP_ERR_INVALID_STATE;
    }

    snprintf(angle_text,
             sizeof(angle_text),
             "DOA %.0f   REL %.0f",
             state->active ? state->angle_deg : 0.0f,
             state->active ? state->relative_deg : 0.0f);
    snprintf(energy_text, sizeof(energy_text), "E %.1f dB", state->energy_db);
    snprintf(mic1_text, sizeof(mic1_text), "%lu", (unsigned long)state->mic1_rms);
    snprintf(mic2_text, sizeof(mic2_text), "%lu", (unsigned long)state->mic2_rms);

    direction_color = ui_direction_color(state->active, state->direction);
    indicator_x = ui_indicator_x_from_relative(state->active ? state->relative_deg : 0.0f);

    xSemaphoreTakeRecursive(s_lvgl_mutex, portMAX_DELAY);

    lv_label_set_text(s_direction_label, ui_direction_text(state->active, state->direction));
    lv_obj_set_style_text_color(s_direction_label, direction_color, 0);

    lv_label_set_text(s_angle_label, angle_text);
    lv_label_set_text(s_energy_label, energy_text);
    lv_label_set_text(s_status_label, state->active ? "Voice active" : "Waiting for voice");
    lv_obj_set_style_text_color(s_status_label, direction_color, 0);

    lv_obj_set_style_bg_color(s_indicator, direction_color, 0);
    lv_obj_set_pos(s_indicator,
                   indicator_x,
                   UI_TRACK_Y - ((UI_INDICATOR_SIZE - UI_TRACK_H) / 2));

    lv_bar_set_value(s_mic1_bar, ui_bar_value_from_rms(state->mic1_rms), LV_ANIM_OFF);
    lv_bar_set_value(s_mic2_bar, ui_bar_value_from_rms(state->mic2_rms), LV_ANIM_OFF);
    lv_label_set_text(s_mic1_value_label, mic1_text);
    lv_label_set_text(s_mic2_value_label, mic2_text);

    xSemaphoreGiveRecursive(s_lvgl_mutex);
    return ESP_OK;
}
