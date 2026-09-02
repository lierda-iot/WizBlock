/* LVGL-based UI for WiFi settings and network status display */
#include "demo_ui.h"

#include "lte_net_display_flush.h"
#include "lte_net_state_view.h"
#include "lte_net_wifi_options.h"
#include "board_laiwfs300.h"
#include "board_pins.h"
#include "display_hal.h"
#include "touch_hal.h"

#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdbool.h>
#include <string.h>

static const char *TAG = "demo_ui";

#define LCD_H_RES             BOARD_LAIWFS300_LCD_H_RES
#define LCD_V_RES             BOARD_LAIWFS300_LCD_V_RES
#define LVGL_HOR_RES          LCD_V_RES
#define LVGL_VER_RES          LCD_H_RES
#define LVGL_BUFFER_ROWS      BOARD_LAIWFS300_LCD_DRAW_BUF_LINES
#define LVGL_TICK_MS          2
#define LVGL_TASK_DELAY_MS    10
#define LVGL_TASK_STACK       8192
#define LVGL_TASK_PRIORITY    2
#define TOUCH_INIT_RETRY_COUNT 3
#define TOUCH_INIT_RETRY_MS    150
#define UI_KEYBOARD_HEADER_HEIGHT 56
#define UI_KEYBOARD_HEIGHT     (LVGL_VER_RES - UI_KEYBOARD_HEADER_HEIGHT)
#define UI_DEBOUNCE_MS        3000
#define UI_DISPLAY_WAIT_TIMEOUT_MS 1000U
#define UI_DISPLAY_ERROR_LOG_INTERVAL 60U

static SemaphoreHandle_t s_lvgl_mutex;
static demo_ui_confirm_cb_t s_confirm_cb;
static void *s_confirm_ctx;
static bool s_ui_ready;

static lv_obj_t *s_settings_page;
static lv_obj_t *s_keyboard_page;
static lv_obj_t *s_network_page;
static lv_obj_t *s_ssid_dropdown;
static lv_obj_t *s_ssid_textarea;
static lv_obj_t *s_ssid_refresh_button;
static lv_obj_t *s_password_textarea;
static lv_obj_t *s_keyboard;
static lv_obj_t *s_keyboard_textarea;
static lv_obj_t *s_keyboard_title_label;
static lv_obj_t *s_keyboard_target;
static lv_obj_t *s_settings_message_label;
static lv_obj_t *s_network_label;
static lv_obj_t *s_network_detail_label;
static lv_obj_t *s_network_spinner;
static lv_obj_t *s_network_settings_button;
static lv_obj_t *s_mode_label;

static lv_color_t *s_lvgl_buf1;
static lv_color_t *s_lvgl_buf2;
static esp_timer_handle_t s_lvgl_tick_timer;
static uint32_t s_display_flush_error_count;
static network_manager_wifi_scan_list_t s_wifi_scan_list;
static lte_net_wifi_options_t s_wifi_options_model;
static char s_wifi_options_text[LTE_NET_WIFI_OPTIONS_BUFFER_SIZE];
static bool s_wifi_scan_pending;
static uint32_t s_wifi_scan_operation_id;

static portMUX_TYPE s_render_lock = portMUX_INITIALIZER_UNLOCKED;
static portMUX_TYPE s_scan_lock = portMUX_INITIALIZER_UNLOCKED;
static demo_net_state_t s_target_state = DEMO_NET_STATE_NONE;
static demo_net_state_t s_rendered_state = DEMO_NET_STATE_NONE;
static demo_net_detail_t s_rendered_detail = DEMO_NET_DETAIL_NONE;
static TickType_t s_target_since_tick;

static int wait_for_display_flush(void *context)
{
    (void)context;
    return (int)display_hal_wait_pending(UI_DISPLAY_WAIT_TIMEOUT_MS);
}

static void notify_lvgl_flush_ready(void *context)
{
    lv_disp_flush_ready((lv_disp_drv_t *)context);
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *color_map)
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
        px[i] = (bgr >> 8) | (bgr << 8);
    }
    esp_err_t result = display_hal_draw_bitmap_rgb565(
        area->x1, area->y1,
        area->x2 - area->x1 + 1,
        area->y2 - area->y1 + 1, px);
    result = (esp_err_t)lte_net_display_flush_complete(
        (int)result, wait_for_display_flush, notify_lvgl_flush_ready, drv);
    if (ESP_OK != result) {
        if (UINT32_MAX != s_display_flush_error_count) {
            s_display_flush_error_count++;
        }
        if ((1U == s_display_flush_error_count) ||
            (0U == (s_display_flush_error_count % UI_DISPLAY_ERROR_LOG_INTERVAL))) {
            ESP_LOGE(TAG, "display flush failed: count=%lu error=%s",
                     (unsigned long)s_display_flush_error_count,
                     esp_err_to_name(result));
        }
    }
}

static void lvgl_tick_cb(void *arg)
{
    (void)arg;
    lv_tick_inc(LVGL_TICK_MS);
}

static const char *ui_current_preferred_ssid(void)
{
    const char *text = (NULL != s_ssid_textarea) ?
                       lv_textarea_get_text(s_ssid_textarea) : NULL;
    return (NULL != text && '\0' != text[0]) ? text : NULL;
}

static void ui_set_manual_ssid_visible(bool visible)
{
    if (NULL == s_ssid_textarea || NULL == s_ssid_dropdown) {
        return;
    }
    if (visible) {
        lv_obj_clear_flag(s_ssid_textarea, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ssid_dropdown, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_ssid_textarea, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_ssid_dropdown, LV_OBJ_FLAG_HIDDEN);
    }
}

static void ui_render_wifi_scan_result(void)
{
    const char *preferred_ssid = ui_current_preferred_ssid();
    if (!lte_net_wifi_options_build(&s_wifi_scan_list, preferred_ssid,
                                    s_wifi_options_text,
                                    sizeof(s_wifi_options_text),
                                    &s_wifi_options_model)) {
        lv_label_set_text(s_settings_message_label, "WiFi list is too long");
        ui_set_manual_ssid_visible(true);
        return;
    }
    lv_dropdown_set_options_static(s_ssid_dropdown, s_wifi_options_text);
    lv_dropdown_set_selected(s_ssid_dropdown,
                             (uint16_t)s_wifi_options_model.selected_index);
    if (s_wifi_options_model.selected_index ==
        s_wifi_options_model.other_index) {
        ui_set_manual_ssid_visible(true);
        return;
    }

    char selected_ssid[DEMO_UI_WIFI_SSID_MAX_LEN + 1] = {0};
    if (lte_net_wifi_options_copy_selection(
        &s_wifi_scan_list, &s_wifi_options_model,
            s_wifi_options_model.selected_index, selected_ssid,
            sizeof(selected_ssid))) {
        lv_textarea_set_text(s_ssid_textarea, selected_ssid);
    }
    ui_set_manual_ssid_visible(false);
}

static void ui_poll_wifi_scan(void)
{
    bool pending = false;
    uint32_t operation_id = 0U;

    portENTER_CRITICAL(&s_scan_lock);
    pending = s_wifi_scan_pending;
    operation_id = s_wifi_scan_operation_id;
    portEXIT_CRITICAL(&s_scan_lock);
    if (!pending) {
        return;
    }

    network_manager_wifi_scan_list_t result = {0};
    if (ESP_OK != network_manager_wifi_scan_get_latest(&result) ||
        !lte_net_wifi_scan_result_matches(operation_id, true, &result)) {
        return;
    }

    portENTER_CRITICAL(&s_scan_lock);
    if (s_wifi_scan_pending && operation_id == s_wifi_scan_operation_id) {
        s_wifi_scan_pending = false;
    }
    portEXIT_CRITICAL(&s_scan_lock);
    s_wifi_scan_list = result;
    if (ESP_OK == result.result) {
        ui_render_wifi_scan_result();
        lv_label_set_text(s_settings_message_label, "Select a network");
    } else {
        lv_dropdown_set_options_static(s_ssid_dropdown,
                                       LTE_NET_WIFI_OTHER_OPTION);
        s_wifi_options_model = (lte_net_wifi_options_t){.other_index = 0U};
        ui_set_manual_ssid_visible(true);
        lv_label_set_text(s_settings_message_label,
                          "WiFi scan failed; enter SSID");
    }
}

static void lvgl_task(void *arg)
{
    (void)arg;
    while (true) {
        xSemaphoreTakeRecursive(s_lvgl_mutex, portMAX_DELAY);
        ui_poll_wifi_scan();
        lv_timer_handler();
        xSemaphoreGiveRecursive(s_lvgl_mutex);
        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_DELAY_MS));
    }
}

static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    touch_panel_point_t point = {0};
    uint8_t count = 0;
    (void)drv;

    esp_err_t ret = touch_panel_read_point(&point, &count);
    if (ESP_OK == ret && count > 0 && count <= 2) {
        last_x = (lv_coord_t)(LCD_V_RES - 1 - point.y);
        last_y = (lv_coord_t)point.x;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    data->point.x = last_x;
    data->point.y = last_y;
}

static void ui_close_keyboard(bool apply_text)
{
    lv_obj_t *target = s_keyboard_target;
    if (NULL == s_keyboard || NULL == s_keyboard_page) {
        return;
    }
    if (apply_text && NULL != target && NULL != s_keyboard_textarea) {
        const char *text = lv_textarea_get_text(s_keyboard_textarea);
        if (NULL != text) {
            lv_textarea_set_text(target, text);
        }
    }
    lv_keyboard_set_textarea(s_keyboard, NULL);
    s_keyboard_target = NULL;
    lv_obj_add_flag(s_keyboard_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
    if (NULL != target) {
        lv_obj_clear_state(target, LV_STATE_FOCUSED);
    }
}

static void ui_show_keyboard(lv_obj_t *target)
{
    if (NULL == target || NULL == s_keyboard || NULL == s_keyboard_page ||
        NULL == s_keyboard_textarea || NULL == s_keyboard_title_label) {
        return;
    }
    bool password_mode = (target == s_password_textarea);
    const char *text = lv_textarea_get_text(target);
    s_keyboard_target = target;

    lv_label_set_text(s_keyboard_title_label, password_mode ? "Password" : "SSID");
    lv_textarea_set_password_mode(s_keyboard_textarea, password_mode);
    lv_textarea_set_max_length(s_keyboard_textarea,
                               password_mode ? DEMO_UI_WIFI_PASSWORD_MAX_LEN
                                             : DEMO_UI_WIFI_SSID_MAX_LEN);
    lv_textarea_set_text(s_keyboard_textarea, (NULL != text) ? text : "");
    lv_textarea_set_cursor_pos(s_keyboard_textarea, LV_TEXTAREA_CURSOR_LAST);
    lv_keyboard_set_textarea(s_keyboard, s_keyboard_textarea);

    lv_obj_add_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_keyboard_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_keyboard_page);
    lv_obj_update_layout(s_keyboard_page);
    lv_obj_invalidate(s_keyboard_page);
}

static void ui_textarea_event_cb(lv_event_t *event)
{
    if (NULL == event || NULL == s_keyboard) {
        return;
    }
    if (LV_EVENT_CLICKED == lv_event_get_code(event)) {
        ui_show_keyboard(lv_event_get_target(event));
    }
}

static void ui_ssid_dropdown_event_cb(lv_event_t *event)
{
    if (NULL == event || LV_EVENT_VALUE_CHANGED != lv_event_get_code(event) ||
        NULL == s_ssid_dropdown) {
        return;
    }
    const uint16_t selected = lv_dropdown_get_selected(s_ssid_dropdown);
    if ((size_t)selected == s_wifi_options_model.other_index) {
        ui_set_manual_ssid_visible(true);
        return;
    }

    char selected_ssid[DEMO_UI_WIFI_SSID_MAX_LEN + 1] = {0};
    if (lte_net_wifi_options_copy_selection(
            &s_wifi_scan_list, &s_wifi_options_model, selected,
            selected_ssid, sizeof(selected_ssid))) {
        lv_textarea_set_text(s_ssid_textarea, selected_ssid);
        ui_set_manual_ssid_visible(false);
    }
}

static void ui_refresh_scan_event_cb(lv_event_t *event)
{
    if (NULL != event && LV_EVENT_CLICKED == lv_event_get_code(event)) {
        const esp_err_t result = demo_ui_start_wifi_scan();
        lv_label_set_text(s_settings_message_label,
                          ESP_OK == result ? "Scanning WiFi..." :
                                             "WiFi scan unavailable");
    }
}

static void ui_keyboard_event_cb(lv_event_t *event)
{
    lv_event_code_t code = lv_event_get_code(event);
    if (LV_EVENT_READY == code) {
        ui_close_keyboard(true);
    } else if (LV_EVENT_CANCEL == code) {
        ui_close_keyboard(false);
    }
}

static void ui_keyboard_back_event_cb(lv_event_t *event)
{
    if (LV_EVENT_CLICKED == lv_event_get_code(event)) {
        ui_close_keyboard(false);
    }
}

static void ui_keyboard_done_event_cb(lv_event_t *event)
{
    if (LV_EVENT_CLICKED == lv_event_get_code(event)) {
        ui_close_keyboard(true);
    }
}
static void ui_confirm_event_cb(lv_event_t *event)
{
    if (LV_EVENT_CLICKED != lv_event_get_code(event)) {
        return;
    }
    const uint16_t selected = (NULL != s_ssid_dropdown) ?
                              lv_dropdown_get_selected(s_ssid_dropdown) : 0U;
    char selected_ssid[DEMO_UI_WIFI_SSID_MAX_LEN + 1] = {0};
    const char *ssid = NULL;
    if ((size_t)selected == s_wifi_options_model.other_index) {
        ssid = lv_textarea_get_text(s_ssid_textarea);
    } else if (lte_net_wifi_options_copy_selection(
                   &s_wifi_scan_list, &s_wifi_options_model, selected,
                   selected_ssid, sizeof(selected_ssid))) {
        ssid = selected_ssid;
    } else {
        ssid = lv_textarea_get_text(s_ssid_textarea);
    }
    const char *password = lv_textarea_get_text(s_password_textarea);
    size_t ssid_len = (NULL != ssid) ? strlen(ssid) : 0;
    size_t password_len = (NULL != password) ? strlen(password) : 0;

    if (0 == ssid_len || DEMO_UI_WIFI_SSID_MAX_LEN < ssid_len) {
        lv_label_set_text(s_settings_message_label, "SSID must be 1-32 characters");
        return;
    }
    if (0 == password_len || DEMO_UI_WIFI_PASSWORD_MAX_LEN < password_len) {
        lv_label_set_text(s_settings_message_label, "Password must be 1-63 characters");
        return;
    }

    ui_close_keyboard(false);
    lv_obj_add_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_network_page, LV_OBJ_FLAG_HIDDEN);

    if (NULL != s_confirm_cb) {
        demo_ui_wifi_settings_t settings = {0};
        memcpy(settings.ssid, ssid, ssid_len);
        memcpy(settings.password, password, password_len);
        s_confirm_cb(&settings, s_confirm_ctx);
    }
}

static void ui_network_settings_event_cb(lv_event_t *event)
{
    if (LV_EVENT_CLICKED != lv_event_get_code(event)) {
        return;
    }
    lv_obj_add_flag(s_network_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_settings_message_label, "Update WiFi settings");
    (void)demo_ui_start_wifi_scan();
}

static lv_obj_t *ui_create_textarea(lv_obj_t *parent, lv_coord_t x,
                                    lv_coord_t y, const char *placeholder,
                                    bool password_mode)
{
    lv_obj_t *textarea = lv_textarea_create(parent);
    lv_obj_set_size(textarea, 205, 36);
    lv_obj_set_pos(textarea, x, y);
    lv_textarea_set_one_line(textarea, true);
    lv_textarea_set_max_length(textarea,
                               password_mode ? DEMO_UI_WIFI_PASSWORD_MAX_LEN
                                             : DEMO_UI_WIFI_SSID_MAX_LEN);
    lv_textarea_set_placeholder_text(textarea, placeholder);
    if (password_mode) {
        lv_textarea_set_password_mode(textarea, true);
    }
    lv_obj_set_style_bg_color(textarea, lv_color_make(245, 248, 252), 0);
    lv_obj_set_style_text_color(textarea, lv_color_make(24, 38, 56), 0);
    lv_obj_set_style_border_width(textarea, 1, 0);
    lv_obj_set_style_border_color(textarea, lv_color_make(150, 170, 195), 0);
    lv_obj_set_style_radius(textarea, 6, 0);
    lv_obj_add_event_cb(textarea, ui_textarea_event_cb, LV_EVENT_ALL, NULL);
    return textarea;
}

static void ui_create_settings_page(lv_obj_t *parent,
                                    const demo_ui_wifi_settings_t *initial)
{
    s_settings_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_settings_page);
    lv_obj_set_size(s_settings_page, LVGL_HOR_RES, LVGL_VER_RES);
    lv_obj_set_style_bg_color(s_settings_page, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_settings_page, LV_OPA_COVER, 0);

    lv_obj_t *title = lv_label_create(s_settings_page);
    lv_label_set_text(title, "WiFi Settings");
    lv_obj_set_style_text_color(title, lv_color_make(20, 56, 98), 0);
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_pos(title, 20, 12);

    lv_obj_t *ssid_label = lv_label_create(s_settings_page);
    lv_label_set_text(ssid_label, "SSID");
    lv_obj_set_style_text_color(ssid_label, lv_color_make(40, 56, 74), 0);
    lv_obj_set_style_text_font(ssid_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(ssid_label, 20, 48);
    s_ssid_dropdown = lv_dropdown_create(s_settings_page);
    lv_obj_set_size(s_ssid_dropdown, 164, 36);
    lv_obj_set_pos(s_ssid_dropdown, 95, 42);
    lv_dropdown_set_options_static(s_ssid_dropdown, LTE_NET_WIFI_OTHER_OPTION);
    lv_obj_set_style_bg_color(s_ssid_dropdown, lv_color_make(245, 248, 252), 0);
    lv_obj_set_style_text_color(s_ssid_dropdown, lv_color_make(24, 38, 56), 0);
    lv_obj_set_style_border_width(s_ssid_dropdown, 1, 0);
    lv_obj_set_style_border_color(s_ssid_dropdown,
                                  lv_color_make(150, 170, 195), 0);
    lv_obj_set_style_radius(s_ssid_dropdown, 6, 0);
    lv_obj_add_event_cb(s_ssid_dropdown, ui_ssid_dropdown_event_cb,
                        LV_EVENT_VALUE_CHANGED, NULL);

    s_ssid_refresh_button = lv_btn_create(s_settings_page);
    lv_obj_set_size(s_ssid_refresh_button, 36, 36);
    lv_obj_set_pos(s_ssid_refresh_button, 265, 42);
    lv_obj_set_style_bg_color(s_ssid_refresh_button,
                              lv_color_make(82, 101, 125), 0);
    lv_obj_set_style_radius(s_ssid_refresh_button, 6, 0);
    lv_obj_add_event_cb(s_ssid_refresh_button, ui_refresh_scan_event_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_label = lv_label_create(s_ssid_refresh_button);
    lv_label_set_text(refresh_label, LV_SYMBOL_REFRESH);
    lv_obj_set_style_text_color(refresh_label, lv_color_white(), 0);
    lv_obj_center(refresh_label);

    s_ssid_textarea = ui_create_textarea(s_settings_page, 95, 42,
                                         "Enter hidden WiFi name", false);
    lv_obj_add_flag(s_ssid_textarea, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *pw_label = lv_label_create(s_settings_page);
    lv_label_set_text(pw_label, "Password");
    lv_obj_set_style_text_color(pw_label, lv_color_make(40, 56, 74), 0);
    lv_obj_set_style_text_font(pw_label, &lv_font_montserrat_16, 0);
    lv_obj_set_pos(pw_label, 20, 96);
    s_password_textarea = ui_create_textarea(s_settings_page, 95, 90,
                                             "WiFi password", true);

    lv_obj_t *hint = lv_label_create(s_settings_page);
    lv_label_set_text(hint, "Select a network or choose Other network");
    lv_obj_set_style_text_color(hint, lv_color_make(110, 128, 148), 0);
    lv_obj_set_style_text_font(hint, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(hint, 20, 132);

    lv_obj_t *confirm_btn = lv_btn_create(s_settings_page);
    lv_obj_set_size(confirm_btn, 120, 42);
    lv_obj_set_pos(confirm_btn, 100, 166);
    lv_obj_set_style_bg_color(confirm_btn, lv_color_make(20, 110, 210), 0);
    lv_obj_set_style_bg_color(confirm_btn, lv_color_make(12, 86, 174),
                              LV_STATE_PRESSED);
    lv_obj_set_style_radius(confirm_btn, 7, 0);
    lv_obj_add_event_cb(confirm_btn, ui_confirm_event_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *confirm_label = lv_label_create(confirm_btn);
    lv_label_set_text(confirm_label, "Confirm");
    lv_obj_set_style_text_color(confirm_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(confirm_label, &lv_font_montserrat_16, 0);
    lv_obj_center(confirm_label);

    s_settings_message_label = lv_label_create(s_settings_page);
    lv_label_set_text(s_settings_message_label, "");
    lv_obj_set_style_text_color(s_settings_message_label,
                                lv_color_make(210, 70, 55), 0);
    lv_obj_set_style_text_font(s_settings_message_label,
                               &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_settings_message_label, LVGL_HOR_RES - 32);
    lv_obj_set_style_text_align(s_settings_message_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_settings_message_label, 16, 214);

    if (NULL != initial && strlen(initial->ssid) > 0) {
        lv_textarea_set_text(s_ssid_textarea, initial->ssid);
        lv_textarea_set_text(s_password_textarea, initial->password);
    }
}
static void ui_create_keyboard_page(lv_obj_t *parent)
{
    s_keyboard_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_keyboard_page);
    lv_obj_set_size(s_keyboard_page, LVGL_HOR_RES, LVGL_VER_RES);
    lv_obj_set_style_bg_color(s_keyboard_page, lv_color_make(228, 236, 246), 0);
    lv_obj_set_style_bg_opa(s_keyboard_page, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_keyboard_page, LV_OBJ_FLAG_HIDDEN);

    lv_obj_t *back_btn = lv_btn_create(s_keyboard_page);
    lv_obj_set_size(back_btn, 44, 40);
    lv_obj_set_pos(back_btn, 6, 8);
    lv_obj_set_style_bg_color(back_btn, lv_color_make(82, 101, 125), 0);
    lv_obj_set_style_radius(back_btn, 6, 0);
    lv_obj_add_event_cb(back_btn, ui_keyboard_back_event_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_lbl = lv_label_create(back_btn);
    lv_label_set_text(back_lbl, "Back");
    lv_obj_set_style_text_color(back_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(back_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(back_lbl);

    s_keyboard_title_label = lv_label_create(s_keyboard_page);
    lv_label_set_text(s_keyboard_title_label, "SSID");
    lv_obj_set_style_text_color(s_keyboard_title_label,
                                lv_color_make(40, 56, 74), 0);
    lv_obj_set_style_text_font(s_keyboard_title_label,
                               &lv_font_montserrat_14, 0);
    lv_obj_set_width(s_keyboard_title_label, 66);
    lv_obj_set_style_text_align(s_keyboard_title_label,
                                LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_pos(s_keyboard_title_label, 52, 20);

    s_keyboard_textarea = lv_textarea_create(s_keyboard_page);
    lv_obj_set_size(s_keyboard_textarea, 128, 40);
    lv_obj_set_pos(s_keyboard_textarea, 118, 8);
    lv_textarea_set_one_line(s_keyboard_textarea, true);
    lv_obj_set_style_bg_color(s_keyboard_textarea, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_keyboard_textarea, LV_OPA_COVER, 0);
    lv_obj_set_style_text_color(s_keyboard_textarea,
                                lv_color_make(24, 38, 56), 0);
    lv_obj_set_style_border_width(s_keyboard_textarea, 1, 0);
    lv_obj_set_style_border_color(s_keyboard_textarea,
                                  lv_color_make(130, 152, 178), 0);
    lv_obj_set_style_radius(s_keyboard_textarea, 6, 0);

    lv_obj_t *done_btn = lv_btn_create(s_keyboard_page);
    lv_obj_set_size(done_btn, 62, 40);
    lv_obj_set_pos(done_btn, 252, 8);
    lv_obj_set_style_bg_color(done_btn, lv_color_make(20, 110, 210), 0);
    lv_obj_set_style_radius(done_btn, 6, 0);
    lv_obj_add_event_cb(done_btn, ui_keyboard_done_event_cb,
                        LV_EVENT_CLICKED, NULL);
    lv_obj_t *done_lbl = lv_label_create(done_btn);
    lv_label_set_text(done_lbl, "Done");
    lv_obj_set_style_text_color(done_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(done_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(done_lbl);

    s_keyboard = lv_keyboard_create(s_keyboard_page);
    lv_obj_set_size(s_keyboard, LVGL_HOR_RES, UI_KEYBOARD_HEIGHT);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_obj_set_style_bg_color(s_keyboard, lv_color_make(202, 214, 228),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_opa(s_keyboard, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_keyboard, lv_color_make(248, 250, 253),
                              LV_PART_ITEMS);
    lv_obj_set_style_bg_opa(s_keyboard, LV_OPA_COVER, LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_keyboard, lv_color_make(25, 44, 68),
                                LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_keyboard, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_keyboard, lv_color_make(145, 164, 186),
                                  LV_PART_ITEMS);
    lv_obj_set_style_radius(s_keyboard, 4, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_keyboard, ui_keyboard_event_cb, LV_EVENT_ALL, NULL);
}

static const char *mode_label_text(network_manager_mode_t mode)
{
    switch (mode) {
        case NETWORK_MANAGER_MODE_WIFI_ONLY: return "WiFi Only";
        case NETWORK_MANAGER_MODE_4G_ONLY:   return "4G Only";
        case NETWORK_MANAGER_MODE_DUAL_AUTO: return "Dual Network";
        default: return "";
    }
}

static void ui_create_network_page(lv_obj_t *parent)
{
    s_network_page = lv_obj_create(parent);
    lv_obj_remove_style_all(s_network_page);
    lv_obj_set_size(s_network_page, LVGL_HOR_RES, LVGL_VER_RES);
    lv_obj_set_style_bg_color(s_network_page, lv_color_white(), 0);
    lv_obj_set_style_bg_opa(s_network_page, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_network_page, LV_OBJ_FLAG_HIDDEN);

    s_mode_label = lv_label_create(s_network_page);
    lv_label_set_text(s_mode_label, "");
    lv_obj_set_style_text_color(s_mode_label, lv_color_make(130, 145, 160), 0);
    lv_obj_set_style_text_font(s_mode_label, &lv_font_montserrat_14, 0);
    lv_obj_set_pos(s_mode_label, LVGL_HOR_RES - 120, 8);

    s_network_spinner = lv_spinner_create(s_network_page, 1000, 60);
    lv_obj_set_size(s_network_spinner, 52, 52);
    lv_obj_set_style_arc_width(s_network_spinner, 5, LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_network_spinner, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_network_spinner, lv_color_make(205, 218, 234),
                               LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_network_spinner, lv_color_make(20, 110, 210),
                               LV_PART_INDICATOR);
    lv_obj_align(s_network_spinner, LV_ALIGN_CENTER, 0, -66);

    s_network_label = lv_label_create(s_network_page);
    lv_label_set_text(s_network_label, "Starting network...");
    lv_obj_set_width(s_network_label, LVGL_HOR_RES - 20);
    lv_obj_set_style_text_align(s_network_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_network_label, lv_color_make(20, 110, 210), 0);
    lv_obj_set_style_text_font(s_network_label, &lv_font_montserrat_20, 0);
    lv_obj_align(s_network_label, LV_ALIGN_CENTER, 0, -10);

    s_network_detail_label = lv_label_create(s_network_page);
    lv_label_set_text(s_network_detail_label, "Preparing WiFi and 4G");
    lv_obj_set_width(s_network_detail_label, LVGL_HOR_RES - 28);
    lv_label_set_long_mode(s_network_detail_label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_align(s_network_detail_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_text_color(s_network_detail_label,
                                lv_color_make(92, 108, 126), 0);
    lv_obj_set_style_text_font(s_network_detail_label,
                               &lv_font_montserrat_14, 0);
    lv_obj_align(s_network_detail_label, LV_ALIGN_CENTER, 0, 28);

    s_network_settings_button = lv_btn_create(s_network_page);
    lv_obj_set_size(s_network_settings_button, 150, 38);
    lv_obj_align(s_network_settings_button, LV_ALIGN_BOTTOM_MID, 0, -12);
    lv_obj_set_style_bg_color(s_network_settings_button,
                              lv_color_make(55, 76, 100), 0);
    lv_obj_set_style_radius(s_network_settings_button, 7, 0);
    lv_obj_add_flag(s_network_settings_button, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(s_network_settings_button,
                        ui_network_settings_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *settings_lbl = lv_label_create(s_network_settings_button);
    lv_label_set_text(settings_lbl, "WiFi Settings");
    lv_obj_set_style_text_color(settings_lbl, lv_color_white(), 0);
    lv_obj_set_style_text_font(settings_lbl, &lv_font_montserrat_14, 0);
    lv_obj_center(settings_lbl);
}
static const char *network_detail_text(demo_net_state_t state,
                                       demo_net_detail_t detail)
{
    switch (detail) {
        case DEMO_NET_DETAIL_WIFI_AUTH_FAILED:
            return "WiFi authentication failed";
        case DEMO_NET_DETAIL_WIFI_NOT_FOUND:
            return "WiFi not found";
        case DEMO_NET_DETAIL_WIFI_DISCONNECTED:
            return "WiFi disconnected";
        case DEMO_NET_DETAIL_WIFI_NO_INTERNET:
            return "WiFi has no internet";
        case DEMO_NET_DETAIL_4G_REMAINS_ONLINE:
            return "4G remains online";
        case DEMO_NET_DETAIL_CONFIG_SAVE_FAILED:
            return "Failed to save WiFi settings";
        case DEMO_NET_DETAIL_LTE_INIT_FAILED:
            return "Failed to power on 4G module";
        case DEMO_NET_DETAIL_NETWORK_INIT_FAILED:
            return "Network initialization failed";
        default:
            break;
    }

    switch (state) {
        case DEMO_NET_STATE_STARTING:
            return "Preparing WiFi and 4G";
        case DEMO_NET_STATE_WIFI_CONNECTING:
            return "Waiting for access point";
        case DEMO_NET_STATE_WIFI_CHECKING:
            return "Checking connection stability";
        case DEMO_NET_STATE_WIFI_CONNECTED:
        case DEMO_NET_STATE_4G_CONNECTED:
            return "Internet is ready";
        case DEMO_NET_STATE_4G_CONNECTING:
            return "Waiting for cellular network";
        case DEMO_NET_STATE_SWITCHING_TO_WIFI:
            return "WiFi passed stability check";
        case DEMO_NET_STATE_NO_NETWORK:
            return "Retrying WiFi and 4G...";
        case DEMO_NET_STATE_ERROR:
            return "Check serial log for details";
        default:
            return "";
    }
}

static void render_network_state_locked(demo_net_state_t state,
                                        demo_net_detail_t detail,
                                        network_manager_mode_t mode)
{
    if (!s_ui_ready || NULL == s_network_label || NULL == s_network_detail_label ||
        NULL == s_network_spinner || NULL == s_network_settings_button ||
        NULL == s_mode_label) {
        return;
    }

    const char *title = NULL;
    lv_color_t state_color = lv_color_make(20, 110, 210);
    bool show_spinner = true;
    bool show_settings = false;

    switch (state) {
        case DEMO_NET_STATE_STARTING:
            title = "Starting network...";
            break;
        case DEMO_NET_STATE_WIFI_CONNECTING:
            title = "Connecting WiFi...";
            break;
        case DEMO_NET_STATE_WIFI_CHECKING:
            title = "Checking WiFi...";
            break;
        case DEMO_NET_STATE_WIFI_CONNECTED:
            title = "WiFi connected";
            state_color = lv_color_make(30, 145, 82);
            show_spinner = false;
            break;
        case DEMO_NET_STATE_4G_CONNECTING:
            title = "Connecting 4G...";
            break;
        case DEMO_NET_STATE_4G_CONNECTED:
            title = "4G connected";
            state_color = lv_color_make(30, 145, 82);
            show_spinner = false;
            break;
        case DEMO_NET_STATE_SWITCHING_TO_WIFI:
            title = "Switching to WiFi...";
            break;
        case DEMO_NET_STATE_NO_NETWORK:
            title = "No network";
            state_color = lv_color_make(196, 120, 20);
            show_settings = true;
            break;
        case DEMO_NET_STATE_ERROR:
            title = "Network error";
            state_color = lv_color_make(202, 62, 55);
            show_spinner = false;
            break;
        default:
            return;
    }

    const char *detail_text = network_detail_text(state, detail);
    lv_label_set_text(s_mode_label, mode_label_text(mode));
    lv_label_set_text(s_network_label, title);
    lv_obj_set_style_text_color(s_network_label, state_color, 0);
    lv_obj_align(s_network_label, LV_ALIGN_CENTER, 0, -10);
    lv_label_set_text(s_network_detail_label, detail_text);
    lv_obj_align(s_network_detail_label, LV_ALIGN_CENTER, 0, 28);
    lv_obj_set_style_arc_color(s_network_spinner, state_color, LV_PART_INDICATOR);

    if (show_spinner) {
        lv_obj_clear_flag(s_network_spinner, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_network_spinner, LV_OBJ_FLAG_HIDDEN);
    }
    if (show_settings) {
        lv_obj_clear_flag(s_network_settings_button, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_network_settings_button, LV_OBJ_FLAG_HIDDEN);
    }
}

esp_err_t demo_ui_init(const demo_ui_config_t *config,
                      const demo_ui_wifi_settings_t *initial_settings)
{
    if (NULL == config) {
        return ESP_ERR_INVALID_ARG;
    }

    s_confirm_cb = config->confirm_cb;
    s_confirm_ctx = config->user_ctx;
    s_wifi_scan_list = (network_manager_wifi_scan_list_t){0};
    s_wifi_options_model = (lte_net_wifi_options_t){.other_index = 0U};
    s_wifi_options_text[0] = '\0';
    portENTER_CRITICAL(&s_scan_lock);
    s_wifi_scan_pending = false;
    s_wifi_scan_operation_id = 0U;
    portEXIT_CRITICAL(&s_scan_lock);

    esp_err_t ret = board_laiwfs300_display_init();
    if (ESP_OK != ret) {
        return ret;
    }
    ret = display_hal_set_orientation(true, false, true);
    if (ESP_OK != ret) {
        return ret;
    }

    bool touch_ok = false;
    for (uint32_t attempt = 1; attempt <= TOUCH_INIT_RETRY_COUNT; attempt++) {
        ret = board_laiwfs300_touch_init();
        if (ESP_OK == ret) {
            touch_ok = true;
            break;
        }
        ESP_LOGW(TAG, "touch init attempt %lu/%d failed: %s",
                 (unsigned long)attempt, TOUCH_INIT_RETRY_COUNT,
                 esp_err_to_name(ret));
        if (attempt < TOUCH_INIT_RETRY_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(TOUCH_INIT_RETRY_MS));
        }
    }
    if (!touch_ok) {
        ESP_LOGE(TAG, "WiFi settings UI requires touch input");
        return ret;
    }

    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (NULL == s_lvgl_mutex) {
        return ESP_ERR_NO_MEM;
    }

    lv_init();

    const size_t buf_pixels = LVGL_HOR_RES * LVGL_BUFFER_ROWS;
    s_lvgl_buf1 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t),
                                   MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    s_lvgl_buf2 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t),
                                   MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == s_lvgl_buf1 || NULL == s_lvgl_buf2) {
        ESP_LOGE(TAG, "LVGL draw buffer alloc failed");
        return ESP_ERR_NO_MEM;
    }

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, s_lvgl_buf1, s_lvgl_buf2, buf_pixels);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LVGL_HOR_RES;
    disp_drv.ver_res = LVGL_VER_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    static lv_indev_drv_t indev_drv;
    lv_indev_drv_init(&indev_drv);
    indev_drv.type = LV_INDEV_TYPE_POINTER;
    indev_drv.read_cb = lvgl_touch_read_cb;
    lv_indev_drv_register(&indev_drv);

    lv_obj_t *scr = lv_scr_act();
    ui_create_network_page(scr);
    ui_create_settings_page(scr, initial_settings);
    ui_create_keyboard_page(scr);

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = true,
    };
    ret = esp_timer_create(&tick_args, &s_lvgl_tick_timer);
    if (ESP_OK != ret) {
        return ret;
    }
    ret = esp_timer_start_periodic(s_lvgl_tick_timer, LVGL_TICK_MS * 1000U);
    if (ESP_OK != ret) {
        return ret;
    }

    BaseType_t ok = xTaskCreate(lvgl_task, "lvgl", LVGL_TASK_STACK, NULL,
                                LVGL_TASK_PRIORITY, NULL);
    if (pdPASS != ok) {
        return ESP_ERR_NO_MEM;
    }

    s_ui_ready = true;
    ESP_LOGI(TAG, "demo UI initialized: %dx%d landscape with touch",
             LVGL_HOR_RES, LVGL_VER_RES);
    return ESP_OK;
}

void demo_ui_update_network_state(demo_net_state_t state,
                                 demo_net_detail_t detail,
                                 network_manager_mode_t mode)
{
    TickType_t now = xTaskGetTickCount();
    bool immediate = lte_net_state_view_is_immediate(state);

    portENTER_CRITICAL(&s_render_lock);
    if (immediate || state != s_target_state) {
        s_target_state = state;
        s_target_since_tick = now;
    }

    bool should_render = immediate ||
                        (now - s_target_since_tick) >= pdMS_TO_TICKS(UI_DEBOUNCE_MS);
    if (should_render && (state != s_rendered_state || detail != s_rendered_detail)) {
        s_rendered_state = state;
        s_rendered_detail = detail;
        portEXIT_CRITICAL(&s_render_lock);

        xSemaphoreTakeRecursive(s_lvgl_mutex, portMAX_DELAY);
        render_network_state_locked(state, detail, mode);
        xSemaphoreGiveRecursive(s_lvgl_mutex);
        return;
    }
    portEXIT_CRITICAL(&s_render_lock);
}

void demo_ui_show_settings_page(void)
{
    if (!s_ui_ready || NULL == s_settings_page || NULL == s_network_page) {
        return;
    }
    xSemaphoreTakeRecursive(s_lvgl_mutex, portMAX_DELAY);
    lv_obj_add_flag(s_network_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
    const esp_err_t result = demo_ui_start_wifi_scan();
    lv_label_set_text(s_settings_message_label,
                      ESP_OK == result ? "Scanning WiFi..." :
                                         "WiFi scan unavailable");
    xSemaphoreGiveRecursive(s_lvgl_mutex);
}

esp_err_t demo_ui_start_wifi_scan(void)
{
    uint32_t operation_id = 0U;
    const esp_err_t result = network_manager_wifi_scan_start(&operation_id);
    if (ESP_OK == result) {
        portENTER_CRITICAL(&s_scan_lock);
        s_wifi_scan_operation_id = operation_id;
        s_wifi_scan_pending = true;
        portEXIT_CRITICAL(&s_scan_lock);
    }
    return result;
}
