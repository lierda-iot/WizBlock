#include "salary_calculator_ui.h"
#include "salary_calculator_logic.h"

#include "board_laiwfs300.h"
#include "board_pins.h"
#include "display_hal.h"
#include "touch_hal.h"

#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_random.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "salary_calc_ui";

#define UI_HOR_RES             BOARD_LAIWFS300_LCD_V_RES
#define UI_VER_RES             BOARD_LAIWFS300_LCD_H_RES
#define UI_BUFFER_ROWS         BOARD_LAIWFS300_LCD_DRAW_BUF_LINES
#define UI_TICK_MS             2
#define UI_TASK_DELAY_MS       10
#define UI_TASK_STACK          8192
#define UI_TASK_PRIORITY       2
#define UI_TASK_CORE           1
#define COIN_COUNT             8
#define COIN_TIMER_MS          50
#define COIN_X_MIN             32
#define COIN_X_RANGE           256U
#define COIN_TARGET_Y_MIN      146
#define COIN_TARGET_Y_RANGE    12U
#define SALARY_STEP_YUAN       1000LL
#define SALARY_MIN_YUAN        1000LL
#define SALARY_MAX_YUAN        999999999LL
#define SOUND_STEP_YUAN        1LL
#define SOUND_MIN_YUAN         1LL
#define SOUND_MAX_YUAN         999999999LL
#define ROLLER_HEIGHT          72
#define MONEY_BG_DATA_SIZE     (UI_HOR_RES * UI_VER_RES * 2U)

#if LV_COLOR_DEPTH != 16
#error "salary_calculator_demo background requires LV_COLOR_DEPTH=16"
#endif

extern const uint8_t money_background_rgb565_start[] asm("_binary_money_background_rgb565_start");

static const lv_img_dsc_t s_money_background_img = {
    .header.always_zero = 0,
    .header.w = UI_HOR_RES,
    .header.h = UI_VER_RES,
    .header.cf = LV_IMG_CF_TRUE_COLOR,
    .data_size = MONEY_BG_DATA_SIZE,
    .data = money_background_rgb565_start,
};

typedef struct {
    lv_obj_t *obj;
    int32_t x_q8;
    int32_t y_q8;
    int32_t vy_q8;
    int32_t drift_q8;
    int16_t target_y;
    uint8_t size;
} coin_sprite_t;

static SemaphoreHandle_t s_lvgl_mutex;
static lv_color_t *s_lvgl_buf1;
static lv_color_t *s_lvgl_buf2;
static esp_timer_handle_t s_lvgl_tick_timer;
static lv_timer_t *s_coin_timer;
static bool s_ui_ready;
static bool s_syncing_current_time;
static bool s_current_time_dirty;
static bool s_money_page_visible;

static salary_ui_callbacks_t s_callbacks;

static lv_obj_t *s_settings_page;
static lv_obj_t *s_money_page;
static lv_obj_t *s_salary_ta;
static lv_obj_t *s_sound_interval_ta;
static lv_obj_t *s_keyboard;
static lv_obj_t *s_rtc_label;
static lv_obj_t *s_rtc_warn_label;
static lv_obj_t *s_settings_message_label;
static lv_obj_t *s_current_date_year_roller;
static lv_obj_t *s_current_date_month_roller;
static lv_obj_t *s_current_date_day_roller;
static lv_obj_t *s_current_time_hour_roller;
static lv_obj_t *s_current_time_minute_roller;
static lv_obj_t *s_current_time_second_roller;
static lv_obj_t *s_start_hour_roller;
static lv_obj_t *s_start_minute_roller;
static lv_obj_t *s_end_hour_roller;
static lv_obj_t *s_end_minute_roller;

static lv_obj_t *s_amount_label;
static lv_obj_t *s_progress_label;

static coin_sprite_t s_coins[COIN_COUNT];
static char s_hour_options[96];
static char s_minute_options[240];
static char s_year_options[640];
static char s_month_options[48];
static char s_day_options[112];

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    uint32_t pixel_count = (uint32_t)(area->x2 - area->x1 + 1) *
                           (uint32_t)(area->y2 - area->y1 + 1);
    uint16_t *px = (uint16_t *)color_map;

    for (uint32_t i = 0; i < pixel_count; i++) {
        uint16_t pixel = px[i];
        uint16_t r = (pixel >> 11) & 0x1F;
        uint16_t g = (pixel >> 5) & 0x3F;
        uint16_t b = pixel & 0x1F;
        uint16_t bgr = (uint16_t)((b << 11) | (g << 5) | r);
        px[i] = (uint16_t)((bgr >> 8) | (bgr << 8));
    }

    display_hal_draw_bitmap_rgb565(area->x1,
                                   area->y1,
                                   area->x2 - area->x1 + 1,
                                   area->y2 - area->y1 + 1,
                                   px);
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
    if (ESP_OK == ret && count > 0 && count <= 2) {
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

static void ui_build_numeric_options(char *buffer,
                                     size_t buffer_size,
                                     unsigned first,
                                     unsigned last,
                                     unsigned width)
{
    size_t offset = 0;

    if (NULL == buffer || 0U == buffer_size || first > last) {
        return;
    }

    memset(buffer, 0, buffer_size);
    for (unsigned value = first; value <= last && offset < buffer_size; value++) {
        int written = snprintf(&buffer[offset], buffer_size - offset,
                               (value < last) ? "%0*u\n" : "%0*u", (int)width, value);

        if (0 >= written || (size_t)written >= (buffer_size - offset)) {
            buffer[buffer_size - 1U] = '\0';
            break;
        }
        offset += (size_t)written;
    }
}

static void ui_build_roller_options(void)
{
    ui_build_numeric_options(s_hour_options, sizeof(s_hour_options), 0U, 23U, 2U);
    ui_build_numeric_options(s_minute_options, sizeof(s_minute_options), 0U, 59U, 2U);
    ui_build_numeric_options(s_year_options, sizeof(s_year_options),
                             SALARY_RTC_YEAR_MIN, SALARY_RTC_YEAR_MAX, 4U);
    ui_build_numeric_options(s_month_options, sizeof(s_month_options), 1U, 12U, 2U);
    ui_build_numeric_options(s_day_options, sizeof(s_day_options), 1U, 31U, 2U);
}

static lv_obj_t *ui_create_field_label(lv_obj_t *parent, const char *text, lv_coord_t x, lv_coord_t y)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_color(label, lv_color_make(156, 170, 188), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(label, x, y);
    return label;
}

static lv_obj_t *ui_create_roller(lv_obj_t *parent,
                                  lv_coord_t x,
                                  lv_coord_t y,
                                  lv_coord_t width,
                                  const char *options)
{
    lv_obj_t *roller = lv_roller_create(parent);

    lv_roller_set_options(roller, options, LV_ROLLER_MODE_NORMAL);
    lv_roller_set_visible_row_count(roller, 3);
    lv_obj_set_size(roller, width, ROLLER_HEIGHT);
    lv_obj_set_style_bg_color(roller, lv_color_make(28, 36, 48), 0);
    lv_obj_set_style_bg_opa(roller, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(roller, lv_color_make(228, 235, 244), 0);
    lv_obj_set_style_text_font(roller, &lv_font_montserrat_20, 0);
    lv_obj_set_style_border_width(roller, 1, 0);
    lv_obj_set_style_border_color(roller, lv_color_make(72, 84, 102), 0);
    lv_obj_set_style_radius(roller, 10, 0);
    lv_obj_set_pos(roller, x, y);
    return roller;
}

static void ui_hide_keyboard(void)
{
    lv_keyboard_set_textarea(s_keyboard, NULL);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
}

static void ui_numeric_textarea_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    lv_obj_t *target = lv_event_get_target(event);

    if (LV_EVENT_FOCUSED == code || LV_EVENT_CLICKED == code) {
        lv_keyboard_set_textarea(s_keyboard, target);
        lv_obj_clear_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_keyboard);
        lv_obj_scroll_to_view_recursive(target, LV_ANIM_ON);
    }
}

static void ui_adjust_numeric_textarea(lv_obj_t *textarea,
                                       int64_t step,
                                       int64_t minimum,
                                       int64_t maximum,
                                       intptr_t direction)
{
    const char *text = NULL;
    char *end = NULL;
    char value_text[24] = {0};
    int64_t value = 0;

    if (NULL == textarea) {
        return;
    }

    text = lv_textarea_get_text(textarea);
    value = strtoll((NULL != text) ? text : "", &end, 10);
    if (NULL == text || end == text || '\0' != *end) {
        value = minimum;
    } else if (0 < direction) {
        if ((maximum - step) < value) {
            value = maximum;
        } else {
            value += step;
        }
    } else if (minimum >= value) {
        value = minimum;
    } else {
        value -= step;
        if (minimum > value) {
            value = minimum;
        }
    }

    snprintf(value_text, sizeof(value_text), "%lld", (long long)value);
    lv_textarea_set_text(textarea, value_text);
}

static void ui_salary_step_event_cb(lv_event_t *event)
{
    if (LV_EVENT_CLICKED == lv_event_get_code(event)) {
        ui_adjust_numeric_textarea(s_salary_ta, SALARY_STEP_YUAN,
                                   SALARY_MIN_YUAN, SALARY_MAX_YUAN,
                                   (intptr_t)lv_event_get_user_data(event));
    }
}

static void ui_sound_step_event_cb(lv_event_t *event)
{
    if (LV_EVENT_CLICKED == lv_event_get_code(event)) {
        ui_adjust_numeric_textarea(s_sound_interval_ta, SOUND_STEP_YUAN,
                                   SOUND_MIN_YUAN, SOUND_MAX_YUAN,
                                   (intptr_t)lv_event_get_user_data(event));
    }
}

static lv_obj_t *ui_create_numeric_step_button(lv_obj_t *parent,
                                                const char *symbol,
                                                lv_coord_t x,
                                                lv_coord_t y,
                                                lv_event_cb_t event_cb,
                                                intptr_t direction)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_t *label = NULL;

    lv_obj_set_size(button, 34, 34);
    lv_obj_set_pos(button, x, y);
    lv_obj_set_style_bg_color(button, lv_color_make(47, 58, 72), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 7, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, (void *)direction);

    label = lv_label_create(button);
    lv_label_set_text(label, symbol);
    lv_obj_set_style_text_color(label, lv_color_make(244, 248, 252), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_center(label);
    return button;
}

static void ui_keyboard_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);

    if (LV_EVENT_READY == code || LV_EVENT_CANCEL == code) {
        ui_hide_keyboard();
        lv_obj_clear_state(s_salary_ta, LV_STATE_FOCUSED);
        lv_obj_clear_state(s_sound_interval_ta, LV_STATE_FOCUSED);
    }
}

static void ui_current_time_roller_event_cb(lv_event_t *event)
{
    if (LV_EVENT_VALUE_CHANGED == lv_event_get_code(event) && !s_syncing_current_time) {
        s_current_time_dirty = true;
    }
}

static void ui_spawn_coin(coin_sprite_t *coin, bool fast)
{
    uint8_t size = (uint8_t)(16 + (esp_random() % 10U));
    int32_t x = COIN_X_MIN + (int32_t)(esp_random() % COIN_X_RANGE);

    if (NULL == coin || NULL == coin->obj) {
        return;
    }

    coin->size = size;
    coin->x_q8 = x << 8;
    coin->y_q8 = -(int32_t)(size << 8);
    coin->vy_q8 = (fast ? 1700 : 700) + (int32_t)(esp_random() % 500U);
    coin->drift_q8 = (int32_t)((int32_t)(esp_random() % 81U) - 40);
    coin->target_y = (int16_t)(COIN_TARGET_Y_MIN + (esp_random() % COIN_TARGET_Y_RANGE));

    lv_obj_set_size(coin->obj, size, size);
    lv_obj_set_pos(coin->obj, x, -(int)size);
    lv_obj_clear_flag(coin->obj, LV_OBJ_FLAG_HIDDEN);
}

static void ui_coin_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    if (!s_money_page_visible) {
        return;
    }

    for (int i = 0; i < COIN_COUNT; i++) {
        coin_sprite_t *coin = &s_coins[i];
        int32_t x = 0;
        int32_t y = 0;

        if (NULL == coin->obj) {
            continue;
        }

        coin->y_q8 += coin->vy_q8;
        coin->x_q8 += coin->drift_q8;
        x = coin->x_q8 >> 8;
        y = coin->y_q8 >> 8;

        if (y >= coin->target_y) {
            ui_spawn_coin(coin, false);
            continue;
        }

        lv_obj_set_pos(coin->obj, x, y);
    }
}

static void ui_confirm_event_cb(lv_event_t *event)
{
    salary_ui_form_t form = {0};

    (void)event;

    if (NULL == s_callbacks.on_confirm) {
        return;
    }

    strncpy(form.salary_text, lv_textarea_get_text(s_salary_ta), sizeof(form.salary_text) - 1);
    strncpy(form.sound_interval_text, lv_textarea_get_text(s_sound_interval_ta),
            sizeof(form.sound_interval_text) - 1);
    form.work_start_hour = (uint8_t)lv_roller_get_selected(s_start_hour_roller);
    form.work_start_minute = (uint8_t)lv_roller_get_selected(s_start_minute_roller);
    form.work_end_hour = (uint8_t)lv_roller_get_selected(s_end_hour_roller);
    form.work_end_minute = (uint8_t)lv_roller_get_selected(s_end_minute_roller);
    form.current_year = (uint16_t)(SALARY_RTC_YEAR_MIN +
                                   lv_roller_get_selected(s_current_date_year_roller));
    form.current_month = (uint8_t)(1U + lv_roller_get_selected(s_current_date_month_roller));
    form.current_day = (uint8_t)(1U + lv_roller_get_selected(s_current_date_day_roller));
    form.current_hour = (uint8_t)lv_roller_get_selected(s_current_time_hour_roller);
    form.current_minute = (uint8_t)lv_roller_get_selected(s_current_time_minute_roller);
    form.current_second = (uint8_t)lv_roller_get_selected(s_current_time_second_roller);
    form.current_time_dirty = s_current_time_dirty;
    s_callbacks.on_confirm(&form, s_callbacks.user_ctx);
}

static void ui_back_event_cb(lv_event_t *event)
{
    (void)event;
    if (NULL != s_callbacks.on_back) {
        s_callbacks.on_back(s_callbacks.user_ctx);
    }
}

static lv_obj_t *ui_create_numeric_textarea(lv_obj_t *parent,
                                             lv_coord_t x,
                                             lv_coord_t y,
                                             const char *placeholder)
{
    lv_obj_t *textarea = lv_textarea_create(parent);

    lv_obj_set_size(textarea, 78, 34);
    lv_obj_set_pos(textarea, x, y);
    lv_textarea_set_one_line(textarea, true);
    lv_textarea_set_max_length(textarea, 9);
    lv_textarea_set_placeholder_text(textarea, placeholder);
    lv_textarea_set_accepted_chars(textarea, "0123456789");
    lv_obj_set_style_bg_color(textarea, lv_color_make(28, 36, 48), 0);
    lv_obj_set_style_text_color(textarea, lv_color_make(236, 242, 248), 0);
    lv_obj_set_style_border_width(textarea, 1, 0);
    lv_obj_set_style_border_color(textarea, lv_color_make(72, 84, 102), 0);
    lv_obj_set_style_radius(textarea, 8, 0);
    lv_obj_set_style_text_align(textarea, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_add_event_cb(textarea, ui_numeric_textarea_event_cb, LV_EVENT_ALL, NULL);
    return textarea;
}

static void ui_create_settings_page(lv_obj_t *parent)
{
    lv_obj_t *title = NULL;
    lv_obj_t *content = NULL;
    lv_obj_t *button = NULL;
    lv_obj_t *button_label = NULL;
    lv_obj_t *salary_note = NULL;
    lv_coord_t y = 0;

    s_settings_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_settings_page);
    lv_obj_set_size(s_settings_page, UI_HOR_RES, UI_VER_RES);
    lv_obj_set_style_bg_color(s_settings_page, lv_color_make(13, 18, 25), 0);
    lv_obj_set_style_bg_opa(s_settings_page, LV_OPA_COVER, 0);

    title = lv_label_create(s_settings_page);
    lv_label_set_text(title, "Salary Calculator");
    lv_obj_set_style_text_color(title, lv_color_make(236, 242, 248), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(title, 16, 10);

    content = lv_obj_create(s_settings_page);
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, UI_HOR_RES - 24, 150);
    lv_obj_set_pos(content, 12, 38);
    lv_obj_set_style_bg_color(content, lv_color_make(13, 18, 25), 0);
    lv_obj_set_style_bg_opa(content, LV_OPA_COVER, 0);
    lv_obj_set_scroll_dir(content, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(content, LV_SCROLLBAR_MODE_AUTO);

    ui_create_field_label(content, "Monthly salary", 2, y);
    ui_create_numeric_step_button(content, "-", 126, y - 4,
                                  ui_salary_step_event_cb, -1);
    s_salary_ta = ui_create_numeric_textarea(content, 164, y - 4, "15000");
    ui_create_numeric_step_button(content, "+", 246, y - 4,
                                  ui_salary_step_event_cb, 1);

    salary_note = lv_label_create(content);
    lv_label_set_text(salary_note, "22 workdays / month");
    lv_obj_set_style_text_color(salary_note, lv_color_make(128, 142, 160), 0);
    lv_obj_set_style_text_font(salary_note, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(salary_note, 126, y + 34);
    y += 58;

    ui_create_field_label(content, "Sound (CNY)", 2, y);
    ui_create_numeric_step_button(content, "-", 126, y - 4,
                                  ui_sound_step_event_cb, -1);
    s_sound_interval_ta = ui_create_numeric_textarea(content, 164, y - 4, "1");
    ui_create_numeric_step_button(content, "+", 246, y - 4,
                                  ui_sound_step_event_cb, 1);
    y += 46;

    ui_create_field_label(content, "Work start", 2, y);
    s_start_hour_roller = ui_create_roller(content, 140, y - 8, 54, s_hour_options);
    s_start_minute_roller = ui_create_roller(content, 212, y - 8, 54, s_minute_options);
    ui_create_field_label(content, ":", 196, y + 12);
    y += 78;

    ui_create_field_label(content, "Work end", 2, y);
    s_end_hour_roller = ui_create_roller(content, 140, y - 8, 54, s_hour_options);
    s_end_minute_roller = ui_create_roller(content, 212, y - 8, 54, s_minute_options);
    ui_create_field_label(content, ":", 196, y + 12);
    y += 78;

    s_rtc_label = lv_label_create(content);
    lv_label_set_text(s_rtc_label, "RTC: --");
    lv_obj_set_style_text_color(s_rtc_label, lv_color_make(184, 194, 208), 0);
    lv_obj_set_style_text_font(s_rtc_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_rtc_label, UI_HOR_RES - 36);
    lv_label_set_long_mode(s_rtc_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_rtc_label, 2, y);
    y += 36;

    s_rtc_warn_label = lv_label_create(content);
    lv_label_set_text(s_rtc_warn_label, "");
    lv_obj_set_style_text_color(s_rtc_warn_label, lv_color_make(255, 189, 89), 0);
    lv_obj_set_style_text_font(s_rtc_warn_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_rtc_warn_label, UI_HOR_RES - 36);
    lv_label_set_long_mode(s_rtc_warn_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_rtc_warn_label, 2, y);
    y += 32;

    ui_create_field_label(content, "Set RTC date", 2, y + 18);
    s_current_date_year_roller = ui_create_roller(content, 94, y - 8, 76, s_year_options);
    s_current_date_month_roller = ui_create_roller(content, 184, y - 8, 44, s_month_options);
    s_current_date_day_roller = ui_create_roller(content, 242, y - 8, 44, s_day_options);
    lv_obj_add_event_cb(s_current_date_year_roller, ui_current_time_roller_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_current_date_month_roller, ui_current_time_roller_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_current_date_day_roller, ui_current_time_roller_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);
    ui_create_field_label(content, "/", 175, y + 12);
    ui_create_field_label(content, "/", 233, y + 12);
    y += 78;

    ui_create_field_label(content, "Set RTC time", 2, y + 18);
    s_current_time_hour_roller = ui_create_roller(content, 104, y - 8, 48, s_hour_options);
    s_current_time_minute_roller = ui_create_roller(content, 168, y - 8, 48, s_minute_options);
    s_current_time_second_roller = ui_create_roller(content, 232, y - 8, 48, s_minute_options);
    lv_obj_add_event_cb(s_current_time_hour_roller, ui_current_time_roller_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_current_time_minute_roller, ui_current_time_roller_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(s_current_time_second_roller, ui_current_time_roller_event_cb, LV_EVENT_VALUE_CHANGED, NULL);
    ui_create_field_label(content, ":", 157, y + 12);
    ui_create_field_label(content, ":", 221, y + 12);
    y += 84;

    s_settings_message_label = lv_label_create(content);
    lv_label_set_text(s_settings_message_label, "");
    lv_obj_set_style_text_color(s_settings_message_label, lv_color_make(128, 142, 160), 0);
    lv_obj_set_style_text_font(s_settings_message_label, &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_settings_message_label, UI_HOR_RES - 36);
    lv_label_set_long_mode(s_settings_message_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_pos(s_settings_message_label, 2, y);

    button = lv_btn_create(s_settings_page);
    lv_obj_set_size(button, UI_HOR_RES - 24, 38);
    lv_obj_set_pos(button, 12, UI_VER_RES - 46);
    lv_obj_set_style_bg_color(button, lv_color_make(245, 170, 52), 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(button, 10, 0);
    lv_obj_add_event_cb(button, ui_confirm_event_cb, LV_EVENT_CLICKED, NULL);

    button_label = lv_label_create(button);
    lv_label_set_text(button_label, "Confirm");
    lv_obj_set_style_text_color(button_label, lv_color_make(22, 24, 30), 0);
    lv_obj_set_style_text_font(button_label, &lv_font_montserrat_20, 0);
    lv_obj_center(button_label);

    s_keyboard = lv_keyboard_create(s_settings_page);
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_NUMBER);
    lv_obj_set_size(s_keyboard, UI_HOR_RES, 108);
    lv_obj_set_pos(s_keyboard, 0, UI_VER_RES - 108);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_FLOATING);
    lv_obj_add_flag(s_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_keyboard, ui_keyboard_event_cb, LV_EVENT_ALL, NULL);
}

static void ui_create_coin_sprite(lv_obj_t *parent, coin_sprite_t *coin)
{
    lv_obj_t *label = NULL;

    coin->obj = lv_obj_create(parent);
    lv_obj_remove_style_all(coin->obj);
    lv_obj_set_size(coin->obj, 18, 18);
    lv_obj_set_style_radius(coin->obj, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(coin->obj, lv_color_make(248, 202, 72), 0);
    lv_obj_set_style_bg_opa(coin->obj, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(coin->obj, 2, 0);
    lv_obj_set_style_border_color(coin->obj, lv_color_make(255, 232, 129), 0);
    lv_obj_add_flag(coin->obj, LV_OBJ_FLAG_HIDDEN);

    label = lv_label_create(coin->obj);
    lv_label_set_text(label, "$");
    lv_obj_set_style_text_color(label, lv_color_make(92, 72, 12), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_center(label);
}

static void ui_create_money_page(lv_obj_t *parent)
{
    lv_obj_t *background = NULL;
    lv_obj_t *back_button = NULL;
    lv_obj_t *back_label = NULL;

    s_money_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_money_page);
    lv_obj_set_size(s_money_page, UI_HOR_RES, UI_VER_RES);
    lv_obj_set_style_bg_color(s_money_page, lv_color_make(24, 24, 30), 0);
    lv_obj_set_style_bg_opa(s_money_page, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_money_page, LV_OBJ_FLAG_HIDDEN);

    background = lv_img_create(s_money_page);
    lv_img_set_src(background, &s_money_background_img);
    lv_obj_set_pos(background, 0, 0);
    lv_obj_clear_flag(background, LV_OBJ_FLAG_CLICKABLE);

    for (int i = 0; i < COIN_COUNT; i++) {
        ui_create_coin_sprite(s_money_page, &s_coins[i]);
        ui_spawn_coin(&s_coins[i], false);
    }

    back_button = lv_btn_create(s_money_page);
    lv_obj_set_size(back_button, 86, 30);
    lv_obj_set_pos(back_button, 12, 12);
    lv_obj_set_style_bg_color(back_button, lv_color_make(55, 56, 66), 0);
    lv_obj_set_style_bg_opa(back_button, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(back_button, 7, 0);
    lv_obj_set_style_shadow_width(back_button, 0, 0);
    lv_obj_add_event_cb(back_button, ui_back_event_cb, LV_EVENT_CLICKED, NULL);

    back_label = lv_label_create(back_button);
    lv_label_set_text(back_label, "< Settings");
    lv_obj_set_style_text_color(back_label, lv_color_make(220, 228, 236), 0);
    lv_obj_set_style_text_font(back_label, &lv_font_montserrat_14, 0);
    lv_obj_center(back_label);

    s_amount_label = lv_label_create(s_money_page);
    lv_label_set_text(s_amount_label, "CNY 0.00");
    lv_obj_set_width(s_amount_label, UI_HOR_RES);
    lv_obj_set_style_text_align(s_amount_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_amount_label, lv_color_make(247, 250, 253), 0);
    lv_obj_set_style_text_font(s_amount_label, &lv_font_montserrat_32, 0);
    lv_obj_set_pos(s_amount_label, 0, 43);

    s_progress_label = lv_label_create(s_money_page);
    lv_label_set_text(s_progress_label, "Today 0%");
    lv_obj_set_width(s_progress_label, 104);
    lv_obj_set_style_text_align(s_progress_label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_style_text_color(s_progress_label, lv_color_make(115, 220, 177), 0);
    lv_obj_set_style_text_font(s_progress_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_progress_label, UI_HOR_RES - 116, 18);
}

static esp_err_t ui_init_screen(void)
{
    lv_obj_t *screen = lv_scr_act();

    lv_obj_set_style_bg_color(screen, lv_color_make(10, 14, 20), 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    ui_build_roller_options();
    ui_create_settings_page(screen);
    ui_create_money_page(screen);

    return ESP_OK;
}

static esp_err_t ui_lock(void)
{
    if (!s_ui_ready || NULL == s_lvgl_mutex) {
        return ESP_ERR_INVALID_STATE;
    }

    if (pdTRUE != xSemaphoreTakeRecursive(s_lvgl_mutex, pdMS_TO_TICKS(1000))) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

static void ui_unlock(void)
{
    if (NULL != s_lvgl_mutex) {
        xSemaphoreGiveRecursive(s_lvgl_mutex);
    }
}

esp_err_t salary_calculator_ui_init(const salary_ui_callbacks_t *callbacks)
{
    lv_disp_draw_buf_t *draw_buf = NULL;
    lv_disp_drv_t *disp_drv = NULL;
    lv_indev_drv_t *indev_drv = NULL;
    size_t buf_pixels = 0;
    BaseType_t ok = pdFAIL;
    esp_err_t ret = ESP_OK;

    if (NULL == callbacks || NULL == callbacks->on_confirm || NULL == callbacks->on_back) {
        return ESP_ERR_INVALID_ARG;
    }

    s_callbacks = *callbacks;

    ret = board_laiwfs300_display_init();
    ESP_RETURN_ON_ERROR(ret, TAG, "display init failed");

    ret = display_hal_set_orientation(true, false, true);
    ESP_RETURN_ON_ERROR(ret, TAG, "display landscape failed");

    ret = board_laiwfs300_touch_init();
    ESP_RETURN_ON_ERROR(ret, TAG, "touch init failed");

    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (NULL == s_lvgl_mutex) {
        return ESP_ERR_NO_MEM;
    }

    lv_init();

    buf_pixels = UI_HOR_RES * UI_BUFFER_ROWS;
    s_lvgl_buf1 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    s_lvgl_buf2 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == s_lvgl_buf1 || NULL == s_lvgl_buf2) {
        heap_caps_free(s_lvgl_buf1);
        heap_caps_free(s_lvgl_buf2);
        return ESP_ERR_NO_MEM;
    }

    draw_buf = heap_caps_malloc(sizeof(*draw_buf), MALLOC_CAP_8BIT);
    disp_drv = heap_caps_malloc(sizeof(*disp_drv), MALLOC_CAP_8BIT);
    indev_drv = heap_caps_malloc(sizeof(*indev_drv), MALLOC_CAP_8BIT);
    if (NULL == draw_buf || NULL == disp_drv || NULL == indev_drv) {
        heap_caps_free(draw_buf);
        heap_caps_free(disp_drv);
        heap_caps_free(indev_drv);
        heap_caps_free(s_lvgl_buf1);
        heap_caps_free(s_lvgl_buf2);
        return ESP_ERR_NO_MEM;
    }

    lv_disp_draw_buf_init(draw_buf, s_lvgl_buf1, s_lvgl_buf2, buf_pixels);
    lv_disp_drv_init(disp_drv);
    disp_drv->hor_res = UI_HOR_RES;
    disp_drv->ver_res = UI_VER_RES;
    disp_drv->flush_cb = lvgl_flush_cb;
    disp_drv->draw_buf = draw_buf;
    lv_disp_drv_register(disp_drv);

    lv_indev_drv_init(indev_drv);
    indev_drv->type = LV_INDEV_TYPE_POINTER;
    indev_drv->read_cb = lvgl_touch_read_cb;
    lv_indev_drv_register(indev_drv);

    {
        const esp_timer_create_args_t tick_args = {
            .callback = lvgl_tick_cb,
            .arg = NULL,
            .dispatch_method = ESP_TIMER_TASK,
            .name = "salary_lvgl",
            .skip_unhandled_events = true,
        };

        ret = esp_timer_create(&tick_args, &s_lvgl_tick_timer);
        ESP_RETURN_ON_ERROR(ret, TAG, "tick timer create failed");
        ret = esp_timer_start_periodic(s_lvgl_tick_timer, UI_TICK_MS * 1000U);
        ESP_RETURN_ON_ERROR(ret, TAG, "tick timer start failed");
    }

    ret = ui_init_screen();
    ESP_RETURN_ON_ERROR(ret, TAG, "screen init failed");

    s_coin_timer = lv_timer_create(ui_coin_timer_cb, COIN_TIMER_MS, NULL);

    ok = xTaskCreatePinnedToCore(lvgl_task, "salary_lvgl", UI_TASK_STACK,
                                 NULL, UI_TASK_PRIORITY, NULL, UI_TASK_CORE);
    if (pdPASS != ok) {
        return ESP_ERR_NO_MEM;
    }

    s_ui_ready = true;
    ESP_LOGI(TAG, "salary calculator UI initialized");
    return ESP_OK;
}

esp_err_t salary_calculator_ui_apply_settings(const salary_ui_settings_state_t *state)
{
    esp_err_t ret = ESP_OK;

    if (NULL == state) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = ui_lock();
    if (ESP_OK != ret) {
        return ret;
    }

    lv_textarea_set_text(s_salary_ta, state->salary_text);
    lv_textarea_set_text(s_sound_interval_ta, state->sound_interval_text);
    lv_roller_set_selected(s_start_hour_roller, state->work_start_hour, LV_ANIM_OFF);
    lv_roller_set_selected(s_start_minute_roller, state->work_start_minute, LV_ANIM_OFF);
    lv_roller_set_selected(s_end_hour_roller, state->work_end_hour, LV_ANIM_OFF);
    lv_roller_set_selected(s_end_minute_roller, state->work_end_minute, LV_ANIM_OFF);

    s_syncing_current_time = true;
    lv_roller_set_selected(s_current_date_year_roller,
                           (uint16_t)(state->current_year - SALARY_RTC_YEAR_MIN), LV_ANIM_OFF);
    lv_roller_set_selected(s_current_date_month_roller,
                           (uint16_t)(state->current_month - 1U), LV_ANIM_OFF);
    lv_roller_set_selected(s_current_date_day_roller,
                           (uint16_t)(state->current_day - 1U), LV_ANIM_OFF);
    lv_roller_set_selected(s_current_time_hour_roller, state->current_hour, LV_ANIM_OFF);
    lv_roller_set_selected(s_current_time_minute_roller, state->current_minute, LV_ANIM_OFF);
    lv_roller_set_selected(s_current_time_second_roller, state->current_second, LV_ANIM_OFF);
    s_current_time_dirty = false;
    s_syncing_current_time = false;

    ui_unlock();
    return ESP_OK;
}

esp_err_t salary_calculator_ui_update_rtc_display(const char *rtc_text, bool rtc_power_lost)
{
    esp_err_t ret = ui_lock();

    if (ESP_OK != ret) {
        return ret;
    }

    lv_label_set_text_fmt(s_rtc_label, "Device RTC: %s", (NULL != rtc_text) ? rtc_text : "--");
    lv_label_set_text(s_rtc_warn_label,
                      rtc_power_lost ? "RTC power-lost flag is set. Adjust time if needed." : "");

    ui_unlock();
    return ESP_OK;
}

esp_err_t salary_calculator_ui_set_message(const char *text, bool is_error)
{
    esp_err_t ret = ui_lock();

    if (ESP_OK != ret) {
        return ret;
    }

    lv_label_set_text(s_settings_message_label, (NULL != text) ? text : "");
    lv_obj_set_style_text_color(s_settings_message_label,
                                is_error ? lv_color_make(255, 120, 120)
                                         : lv_color_make(128, 142, 160),
                                0);

    ui_unlock();
    return ESP_OK;
}

esp_err_t salary_calculator_ui_update_money(const salary_ui_money_state_t *state)
{
    esp_err_t ret = ESP_OK;

    if (NULL == state) {
        return ESP_ERR_INVALID_ARG;
    }

    ret = ui_lock();
    if (ESP_OK != ret) {
        return ret;
    }

    lv_label_set_text(s_amount_label, state->amount_text);
    lv_label_set_text(s_progress_label, state->progress_text);

    ui_unlock();
    return ESP_OK;
}

esp_err_t salary_calculator_ui_show_money_page(void)
{
    esp_err_t ret = ui_lock();

    if (ESP_OK != ret) {
        return ret;
    }

    ui_hide_keyboard();
    lv_obj_add_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_money_page, LV_OBJ_FLAG_HIDDEN);
    s_money_page_visible = true;

    for (int i = 0; i < COIN_COUNT; i++) {
        ui_spawn_coin(&s_coins[i], false);
    }

    ui_unlock();
    return ESP_OK;
}

esp_err_t salary_calculator_ui_show_settings_page(void)
{
    esp_err_t ret = ui_lock();

    if (ESP_OK != ret) {
        return ret;
    }

    lv_obj_add_flag(s_money_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
    s_money_page_visible = false;

    ui_unlock();
    return ESP_OK;
}

esp_err_t salary_calculator_ui_trigger_coin_burst(void)
{
    esp_err_t ret = ui_lock();

    if (ESP_OK != ret) {
        return ret;
    }

    for (int i = 0; i < COIN_COUNT; i++) {
        ui_spawn_coin(&s_coins[i], true);
    }

    ui_unlock();
    return ESP_OK;
}
