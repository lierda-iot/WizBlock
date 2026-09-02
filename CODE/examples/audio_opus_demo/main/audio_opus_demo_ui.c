#include "audio_opus_demo_ui.h"

#include "board_laiwfs300.h"
#include "board_pins.h"
#include "display_hal.h"
#include "touch_hal.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdbool.h>
#include <stdio.h>

static const char *TAG = "audio_opus_demo_ui";

#define UI_HOR_RES                BOARD_LAIWFS300_LCD_V_RES
#define UI_VER_RES                BOARD_LAIWFS300_LCD_H_RES
#define UI_BUFFER_ROWS            BOARD_LAIWFS300_LCD_DRAW_BUF_LINES
#define UI_TICK_MS                2U
#define UI_TASK_DELAY_MS          10U
#define UI_TASK_STACK             8192U
#define UI_TASK_PRIORITY          2U
#define UI_TASK_CORE              1U
#define UI_FIRST_FRAME_TIMEOUT_MS 200U
#define UI_STAGE_COUNT            4U
#define UI_STAGE_DOT_SIZE         8U
#define UI_PROGRESS_BAR_X         20
#define UI_PROGRESS_BAR_Y         134
#define UI_PROGRESS_BAR_W         280
#define UI_PROGRESS_BAR_H         12

static SemaphoreHandle_t s_lvgl_mutex;
static lv_color_t *s_lvgl_buf1;
static lv_color_t *s_lvgl_buf2;
static esp_timer_handle_t s_lvgl_tick_timer;
static bool s_ui_ready;
static audio_opus_demo_ui_callbacks_t s_callbacks;

static lv_obj_t *s_stage_dots[UI_STAGE_COUNT];
static lv_obj_t *s_state_label;
static lv_obj_t *s_detail_label;
static lv_obj_t *s_progress_bar;
static lv_obj_t *s_progress_label;
static lv_obj_t *s_metrics_label;
static lv_obj_t *s_restart_button;
static lv_obj_t *s_restart_button_label;

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    uint32_t pixel_count = (uint32_t)(area->x2 - area->x1 + 1) *
                           (uint32_t)(area->y2 - area->y1 + 1);
    uint16_t *pixels = (uint16_t *)color_map;

    for (uint32_t i = 0; i < pixel_count; i++) {
        uint16_t pixel = pixels[i];
        uint16_t red = (pixel >> 11) & 0x1FU;
        uint16_t green = (pixel >> 5) & 0x3FU;
        uint16_t blue = pixel & 0x1FU;
        uint16_t bgr = (uint16_t)((blue << 11) | (green << 5) | red);

        pixels[i] = (uint16_t)((bgr >> 8) | (bgr << 8));
    }

    display_hal_draw_bitmap_rgb565(area->x1,
                                   area->y1,
                                   area->x2 - area->x1 + 1,
                                   area->y2 - area->y1 + 1,
                                   pixels);
    lv_disp_flush_ready(drv);
}

static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    touch_panel_point_t point = {0};
    uint8_t count = 0;
    esp_err_t ret = ESP_OK;

    (void)drv;

    ret = touch_panel_read_point(&point, &count);
    if (ESP_OK == ret && 0U < count && 2U >= count) {
        last_x = (lv_coord_t)(BOARD_LAIWFS300_LCD_V_RES - 1 - point.y);
        last_y = (lv_coord_t)point.x;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    data->point.x = last_x;
    data->point.y = last_y;
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

static lv_color_t ui_state_color(audio_opus_demo_state_t state)
{
    switch (state) {
    case AUDIO_OPUS_DEMO_STATE_RECORDING:
        return lv_color_make(246, 102, 94);
    case AUDIO_OPUS_DEMO_STATE_ENCODING:
        return lv_color_make(245, 181, 74);
    case AUDIO_OPUS_DEMO_STATE_PLAYING:
        return lv_color_make(74, 192, 218);
    case AUDIO_OPUS_DEMO_STATE_COMPLETE:
        return lv_color_make(92, 210, 145);
    case AUDIO_OPUS_DEMO_STATE_ERROR:
        return lv_color_make(255, 112, 112);
    case AUDIO_OPUS_DEMO_STATE_PREPARING:
    default:
        return lv_color_make(150, 166, 188);
    }
}

static const char *ui_state_text(audio_opus_demo_state_t state)
{
    switch (state) {
    case AUDIO_OPUS_DEMO_STATE_RECORDING:
        return "RECORDING";
    case AUDIO_OPUS_DEMO_STATE_ENCODING:
        return "ENCODING";
    case AUDIO_OPUS_DEMO_STATE_PLAYING:
        return "PLAYING";
    case AUDIO_OPUS_DEMO_STATE_COMPLETE:
        return "COMPLETE";
    case AUDIO_OPUS_DEMO_STATE_ERROR:
        return "ERROR";
    case AUDIO_OPUS_DEMO_STATE_PREPARING:
    default:
        return "PREPARING";
    }
}

static int ui_active_stage(audio_opus_demo_state_t state)
{
    switch (state) {
    case AUDIO_OPUS_DEMO_STATE_RECORDING:
        return 0;
    case AUDIO_OPUS_DEMO_STATE_ENCODING:
        return 1;
    case AUDIO_OPUS_DEMO_STATE_PLAYING:
        return 2;
    case AUDIO_OPUS_DEMO_STATE_COMPLETE:
        return 3;
    case AUDIO_OPUS_DEMO_STATE_PREPARING:
    case AUDIO_OPUS_DEMO_STATE_ERROR:
    default:
        return -1;
    }
}

static void ui_restart_event_cb(lv_event_t *event)
{
    if (LV_EVENT_CLICKED != lv_event_get_code(event)) {
        return;
    }
    if (NULL != s_callbacks.on_restart) {
        s_callbacks.on_restart(s_callbacks.user_ctx);
    }
}

static void ui_create_stage(lv_obj_t *screen, uint32_t index, const char *text, lv_coord_t center_x)
{
    lv_obj_t *label = NULL;

    s_stage_dots[index] = lv_obj_create(screen);
    lv_obj_remove_style_all(s_stage_dots[index]);
    lv_obj_set_size(s_stage_dots[index], UI_STAGE_DOT_SIZE, UI_STAGE_DOT_SIZE);
    lv_obj_set_pos(s_stage_dots[index], center_x - (UI_STAGE_DOT_SIZE / 2), 37);
    lv_obj_set_style_radius(s_stage_dots[index], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_stage_dots[index], lv_color_make(62, 73, 88), 0);
    lv_obj_set_style_bg_opa(s_stage_dots[index], LV_OPA_COVER, 0);

    label = lv_label_create(screen);
    lv_label_set_text(label, text);
    lv_obj_set_width(label, 66);
    lv_obj_set_pos(label, center_x - 33, 49);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(label, lv_color_make(132, 146, 164), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
}

static esp_err_t ui_init_screen(void)
{
    static const char *stage_text[UI_STAGE_COUNT] = {"REC", "ENC", "PLAY", "DONE"};
    static const lv_coord_t stage_x[UI_STAGE_COUNT] = {42, 120, 200, 278};
    lv_obj_t *screen = lv_scr_act();
    lv_obj_t *title = NULL;

    lv_obj_set_style_bg_color(screen, lv_color_make(11, 16, 23), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    title = lv_label_create(screen);
    lv_label_set_text(title, "OPUS RECORD & PLAYBACK");
    lv_obj_set_width(title, UI_HOR_RES);
    lv_obj_set_pos(title, 0, 10);
    lv_obj_set_style_text_align(title, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(title, lv_color_make(178, 190, 207), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_14, 0);

    for (uint32_t i = 0; i < UI_STAGE_COUNT; i++) {
        if (0U < i) {
            lv_obj_t *connector = lv_obj_create(screen);

            lv_obj_remove_style_all(connector);
            lv_obj_set_size(connector, stage_x[i] - stage_x[i - 1] - 16, 2);
            lv_obj_set_pos(connector, stage_x[i - 1] + 8, 40);
            lv_obj_set_style_bg_color(connector, lv_color_make(45, 55, 68), 0);
            lv_obj_set_style_bg_opa(connector, LV_OPA_COVER, 0);
        }
        ui_create_stage(screen, i, stage_text[i], stage_x[i]);
    }

    s_state_label = lv_label_create(screen);
    lv_label_set_text(s_state_label, "PREPARING");
    lv_obj_set_width(s_state_label, UI_HOR_RES);
    lv_obj_set_pos(s_state_label, 0, 70);
    lv_obj_set_style_text_align(s_state_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_state_label, ui_state_color(AUDIO_OPUS_DEMO_STATE_PREPARING), 0);
    lv_obj_set_style_text_font(s_state_label, &lv_font_montserrat_32, 0);

    s_detail_label = lv_label_create(screen);
    lv_label_set_text(s_detail_label, "Initializing audio pipeline");
    lv_obj_set_width(s_detail_label, UI_HOR_RES - 32);
    lv_obj_set_pos(s_detail_label, 16, 108);
    lv_obj_set_style_text_align(s_detail_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_detail_label, lv_color_make(198, 207, 220), 0);
    lv_obj_set_style_text_font(s_detail_label, &lv_font_montserrat_14, 0);

    s_progress_bar = lv_bar_create(screen);
    lv_obj_set_size(s_progress_bar, UI_PROGRESS_BAR_W, UI_PROGRESS_BAR_H);
    lv_obj_set_pos(s_progress_bar, UI_PROGRESS_BAR_X, UI_PROGRESS_BAR_Y);
    lv_bar_set_range(s_progress_bar, 0, 100);
    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_make(35, 45, 58), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_progress_bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress_bar, ui_state_color(AUDIO_OPUS_DEMO_STATE_PREPARING), LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_progress_bar, LV_OPA_COVER, LV_PART_INDICATOR);
    lv_obj_set_style_radius(s_progress_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);

    s_progress_label = lv_label_create(screen);
    lv_label_set_text(s_progress_label, "0%");
    lv_obj_set_width(s_progress_label, UI_HOR_RES - 40);
    lv_obj_set_pos(s_progress_label, 20, 150);
    lv_obj_set_style_text_align(s_progress_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_progress_label, lv_color_make(142, 157, 176), 0);
    lv_obj_set_style_text_font(s_progress_label, &lv_font_montserrat_14, 0);

    s_metrics_label = lv_label_create(screen);
    lv_label_set_text(s_metrics_label, "Frames: --    Compression: --");
    lv_obj_set_width(s_metrics_label, UI_HOR_RES - 32);
    lv_obj_set_pos(s_metrics_label, 16, 169);
    lv_obj_set_style_text_align(s_metrics_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_metrics_label, lv_color_make(169, 181, 197), 0);
    lv_obj_set_style_text_font(s_metrics_label, &lv_font_montserrat_14, 0);

    s_restart_button = lv_btn_create(screen);
    lv_obj_set_size(s_restart_button, UI_HOR_RES - 32, 38);
    lv_obj_set_pos(s_restart_button, 16, UI_VER_RES - 46);
    lv_obj_set_style_radius(s_restart_button, 7, 0);
    lv_obj_set_style_shadow_width(s_restart_button, 0, 0);
    lv_obj_set_style_bg_color(s_restart_button, lv_color_make(54, 65, 79), 0);
    lv_obj_set_style_bg_opa(s_restart_button, LV_OPA_COVER, 0);
    lv_obj_add_state(s_restart_button, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_restart_button, ui_restart_event_cb, LV_EVENT_CLICKED, NULL);

    s_restart_button_label = lv_label_create(s_restart_button);
    lv_label_set_text(s_restart_button_label, "Running...");
    lv_obj_set_style_text_color(s_restart_button_label, lv_color_make(142, 153, 168), 0);
    lv_obj_set_style_text_font(s_restart_button_label, &lv_font_montserrat_20, 0);
    lv_obj_center(s_restart_button_label);

    return ESP_OK;
}

static esp_err_t ui_present_first_frame(void)
{
    lv_timer_handler();
    return display_hal_wait_pending(UI_FIRST_FRAME_TIMEOUT_MS);
}

static void ui_format_detail(const audio_opus_demo_status_t *status, char *text, size_t text_len)
{
    if (NULL == status || NULL == text || 0U == text_len) {
        return;
    }

    switch (status->state) {
    case AUDIO_OPUS_DEMO_STATE_RECORDING:
        snprintf(text, text_len, "%u.%u / %u.%u s",
                 (unsigned)(status->completed_units / 10U),
                 (unsigned)(status->completed_units % 10U),
                 (unsigned)(status->total_units / 10U),
                 (unsigned)(status->total_units % 10U));
        break;
    case AUDIO_OPUS_DEMO_STATE_ENCODING:
        snprintf(text, text_len, "%u / %u frames",
                 (unsigned)status->completed_units, (unsigned)status->total_units);
        break;
    case AUDIO_OPUS_DEMO_STATE_PLAYING:
        snprintf(text, text_len, "%u / %u frames",
                 (unsigned)status->completed_units, (unsigned)status->total_units);
        break;
    case AUDIO_OPUS_DEMO_STATE_COMPLETE:
        snprintf(text, text_len, "Cycle complete");
        break;
    case AUDIO_OPUS_DEMO_STATE_ERROR:
        snprintf(text, text_len, "%s", ('\0' != status->error_text[0]) ? status->error_text : "Audio cycle failed");
        break;
    case AUDIO_OPUS_DEMO_STATE_PREPARING:
    default:
        snprintf(text, text_len, "Initializing audio pipeline");
        break;
    }
}

esp_err_t audio_opus_demo_ui_init(const audio_opus_demo_ui_callbacks_t *callbacks)
{
    static lv_disp_draw_buf_t draw_buf;
    static lv_disp_drv_t disp_drv;
    static lv_indev_drv_t indev_drv;
    bool touch_ok = false;
    size_t buffer_pixels = 0;
    BaseType_t task_ok = pdFAIL;
    esp_err_t ret = ESP_OK;

    if (NULL == callbacks || NULL == callbacks->on_restart) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_ui_ready) {
        return ESP_OK;
    }
    s_callbacks = *callbacks;

    ret = board_laiwfs300_display_init();
    ESP_RETURN_ON_ERROR(ret, TAG, "display init failed");

    ret = display_hal_set_orientation(true, false, true);
    ESP_RETURN_ON_ERROR(ret, TAG, "display landscape failed");

    ret = board_laiwfs300_touch_init();
    if (ESP_OK == ret) {
        touch_ok = true;
    } else {
        ESP_LOGW(TAG, "touch init failed: %s", esp_err_to_name(ret));
    }

    ret = board_laiwfs300_display_fill_rgb565(DISPLAY_HAL_RGB565_WHITE);
    ESP_RETURN_ON_ERROR(ret, TAG, "display first fill failed");

    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (NULL == s_lvgl_mutex) {
        return ESP_ERR_NO_MEM;
    }

    lv_init();
    buffer_pixels = UI_HOR_RES * UI_BUFFER_ROWS;
    s_lvgl_buf1 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    s_lvgl_buf2 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == s_lvgl_buf1 || NULL == s_lvgl_buf2) {
        heap_caps_free(s_lvgl_buf1);
        heap_caps_free(s_lvgl_buf2);
        return ESP_ERR_NO_MEM;
    }

    lv_disp_draw_buf_init(&draw_buf, s_lvgl_buf1, s_lvgl_buf2, buffer_pixels);
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = UI_HOR_RES;
    disp_drv.ver_res = UI_VER_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    if (touch_ok) {
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = lvgl_touch_read_cb;
        lv_indev_drv_register(&indev_drv);
    }

    ESP_RETURN_ON_ERROR(ui_init_screen(), TAG, "screen init failed");
    ESP_RETURN_ON_ERROR(ui_present_first_frame(), TAG, "first frame failed");

    {
        const esp_timer_create_args_t tick_args = {
            .callback = lvgl_tick_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "opus_lvgl_tick",
            .skip_unhandled_events = true,
        };

        ret = esp_timer_create(&tick_args, &s_lvgl_tick_timer);
        ESP_RETURN_ON_ERROR(ret, TAG, "tick timer create failed");
        ret = esp_timer_start_periodic(s_lvgl_tick_timer, UI_TICK_MS * 1000U);
        ESP_RETURN_ON_ERROR(ret, TAG, "tick timer start failed");
    }

    task_ok = xTaskCreatePinnedToCore(lvgl_task, "opus_lvgl", UI_TASK_STACK, NULL,
                                      UI_TASK_PRIORITY, NULL, UI_TASK_CORE);
    if (pdPASS != task_ok) {
        return ESP_ERR_NO_MEM;
    }

    s_ui_ready = true;
    ESP_LOGI(TAG, "Opus status UI initialized: %dx%d%s",
             UI_HOR_RES, UI_VER_RES, touch_ok ? " with touch" : " without touch");
    return ESP_OK;
}

esp_err_t audio_opus_demo_ui_update(const audio_opus_demo_status_t *status)
{
    char detail_text[80] = {0};
    char progress_text[16] = {0};
    char metrics_text[64] = {0};
    bool restart_allowed = false;
    int active_stage = -1;
    lv_color_t state_color = lv_color_make(150, 166, 188);

    if (!s_ui_ready || NULL == status) {
        return ESP_ERR_INVALID_STATE;
    }

    ui_format_detail(status, detail_text, sizeof(detail_text));
    snprintf(progress_text, sizeof(progress_text), "%u%%", (unsigned)status->progress_percent);
    if (0U < status->packet_count || AUDIO_OPUS_DEMO_STATE_COMPLETE == status->state) {
        snprintf(metrics_text, sizeof(metrics_text), "Frames: %lu    Compression: %lu%%",
                 (unsigned long)status->packet_count,
                 (unsigned long)status->compression_percent);
    } else {
        snprintf(metrics_text, sizeof(metrics_text), "Frames: --    Compression: --");
    }

    restart_allowed = audio_opus_demo_restart_allowed(status->state);
    active_stage = ui_active_stage(status->state);
    state_color = ui_state_color(status->state);

    if (pdTRUE != xSemaphoreTakeRecursive(s_lvgl_mutex, pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }

    lv_label_set_text(s_state_label, ui_state_text(status->state));
    lv_obj_set_style_text_color(s_state_label, state_color, 0);
    lv_label_set_text(s_detail_label, detail_text);
    lv_bar_set_value(s_progress_bar, status->progress_percent, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_progress_bar, state_color, LV_PART_INDICATOR);
    lv_label_set_text(s_progress_label, progress_text);
    lv_label_set_text(s_metrics_label, metrics_text);

    for (uint32_t i = 0; i < UI_STAGE_COUNT; i++) {
        lv_color_t stage_color = lv_color_make(62, 73, 88);

        if (AUDIO_OPUS_DEMO_STATE_COMPLETE == status->state ||
            (0 <= active_stage && (int)i < active_stage)) {
            stage_color = ui_state_color(AUDIO_OPUS_DEMO_STATE_COMPLETE);
        } else if ((int)i == active_stage) {
            stage_color = state_color;
        }
        lv_obj_set_style_bg_color(s_stage_dots[i], stage_color, 0);
    }

    if (restart_allowed) {
        lv_obj_clear_state(s_restart_button, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(s_restart_button, state_color, 0);
        lv_label_set_text(s_restart_button_label,
                          (AUDIO_OPUS_DEMO_STATE_ERROR == status->state)
                              ? LV_SYMBOL_REFRESH " Retry"
                              : LV_SYMBOL_REFRESH " Record again");
        lv_obj_set_style_text_color(s_restart_button_label, lv_color_make(16, 23, 30), 0);
    } else {
        lv_obj_add_state(s_restart_button, LV_STATE_DISABLED);
        lv_obj_set_style_bg_color(s_restart_button, lv_color_make(54, 65, 79), 0);
        lv_label_set_text(s_restart_button_label, "Running...");
        lv_obj_set_style_text_color(s_restart_button_label, lv_color_make(142, 153, 168), 0);
    }
    lv_obj_center(s_restart_button_label);

    xSemaphoreGiveRecursive(s_lvgl_mutex);
    return ESP_OK;
}
