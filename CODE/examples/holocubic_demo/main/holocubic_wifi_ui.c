#include "holocubic_wifi_ui.h"

#include "display_hal.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "holocubic_display_domain.h"
#include "holocubic_ui_clock.h"
#include "holocubic_wifi_buffer_policy.h"
#include "lvgl.h"
#include "network_manager.h"
#include "touch_hal.h"

#include <stdio.h>
#include <string.h>

#define HOLO_WIFI_UI_WIDTH 320U
#define HOLO_WIFI_UI_HEIGHT 240U
#define HOLO_WIFI_UI_TASK_DELAY_MS 10U
#define HOLO_WIFI_UI_MAX_SSID_TEXT (NETWORK_MANAGER_WIFI_SSID_MAX_BYTES + 1U)

static const char *TAG = "holocubic_wifi_ui";
static holocubic_ui_state_t *s_state;
static lv_obj_t *s_config_page;
static lv_obj_t *s_keyboard_page;
static lv_obj_t *s_ssid_dropdown;
static lv_obj_t *s_password_textarea;
static lv_obj_t *s_status_label;
static lv_obj_t *s_refresh_button;
static lv_obj_t *s_keyboard;
static lv_obj_t *s_keyboard_textarea;
static uint16_t *s_draw_buffer_1;
static uint16_t *s_draw_buffer_2;
static network_manager_wifi_scan_list_t s_scan_list;
static bool s_ready;
static bool s_scan_pending;
static volatile bool s_open_requested;

static lv_color_t color_hex(uint32_t value)
{
    return lv_color_hex(value);
}

static void style_panel(lv_obj_t *object, lv_color_t background)
{
    lv_obj_set_style_bg_color(object, background, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
}

static lv_obj_t *make_label(lv_obj_t *parent, int x, int y, int width,
                            int height, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, color_hex(0xE7EEF2), 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(label, (NULL != text) ? text : "");
    return label;
}

static void flush_callback(lv_disp_drv_t *driver, const lv_area_t *area,
                           lv_color_t *color_map)
{
    const uint32_t count = (uint32_t)(area->x2 - area->x1 + 1) *
                           (uint32_t)(area->y2 - area->y1 + 1);
    uint16_t *pixels = (uint16_t *)color_map;
    esp_err_t result = ESP_OK;

    for (uint32_t index = 0U; index < count; ++index) {
        const uint16_t pixel = pixels[index];
        const uint16_t red = (pixel >> 11U) & 0x1FU;
        const uint16_t green = (pixel >> 5U) & 0x3FU;
        const uint16_t blue = pixel & 0x1FU;
        const uint16_t bgr = (uint16_t)((blue << 11U) | (green << 5U) | red);
        pixels[index] = (uint16_t)((bgr >> 8U) | (bgr << 8U));
    }
    result = holocubic_display_domain_lock(1000);
    if (ESP_OK == result) {
        result = display_hal_draw_bitmap_rgb565(area->x1, area->y1,
                                                area->x2 - area->x1 + 1,
                                                area->y2 - area->y1 + 1,
                                                pixels);
        if (ESP_OK == result) {
            result = display_hal_wait_pending(1000);
        }
        holocubic_display_domain_unlock();
    }
    if (ESP_OK != result) {
        ESP_LOGW(TAG, "full-screen UI flush failed: %s",
                 esp_err_to_name(result));
    }
    lv_disp_flush_ready(driver);
}

static void touch_read_callback(lv_indev_drv_t *driver, lv_indev_data_t *data)
{
    static lv_coord_t last_x;
    static lv_coord_t last_y;
    touch_panel_point_t point = {0};
    uint8_t count = 0U;

    (void)driver;
    if (NULL == data) return;
    if (!holocubic_ui_wifi_input_enabled(s_state)) {
        data->state = LV_INDEV_STATE_REL;
        data->point.x = last_x;
        data->point.y = last_y;
        return;
    }
    if (ESP_OK == touch_panel_read_point(&point, &count) && 0U < count &&
        count <= 2U) {
        last_x = (lv_coord_t)(HOLO_WIFI_UI_WIDTH - 1U - point.y);
        last_y = (lv_coord_t)point.x;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    data->point.x = last_x;
    data->point.y = last_y;
}

static void set_status(const char *text, uint32_t color)
{
    if (NULL != s_status_label) {
        lv_label_set_text(s_status_label, (NULL != text) ? text : "");
        lv_obj_set_style_text_color(s_status_label, color_hex(color), 0);
    }
}

static void render_scan_list(void)
{
    static char options[NETWORK_MANAGER_WIFI_SCAN_MAX_RESULTS *
                        HOLO_WIFI_UI_MAX_SSID_TEXT];
    size_t offset = 0U;

    if (NULL == s_ssid_dropdown) return;
    options[0] = '\0';
    for (size_t index = 0U; index < s_scan_list.count; ++index) {
        const network_manager_wifi_scan_entry_t *entry =
            &s_scan_list.entries[index];
        if (index > 0U && offset + 1U < sizeof(options)) {
            options[offset++] = '\n';
        }
        if (offset + entry->ssid_len + 1U >= sizeof(options)) break;
        memcpy(&options[offset], entry->ssid, entry->ssid_len);
        offset += entry->ssid_len;
        options[offset] = '\0';
    }
    if (0U == s_scan_list.count) {
        lv_dropdown_set_options_static(s_ssid_dropdown, "扫描不到加密网络");
    } else {
        lv_dropdown_set_options_static(s_ssid_dropdown, options);
    }
}

static void request_scan(void)
{
    uint32_t operation_id = 0U;
    const esp_err_t result = network_manager_wifi_scan_start(&operation_id);

    if (ESP_OK == result) {
        s_scan_pending = true;
        set_status("SCANNING...", 0x58C7D8);
    } else if (ESP_ERR_INVALID_STATE == result) {
        set_status("SCAN BUSY", 0xE4B34F);
    } else {
        set_status("SCAN FAILED", 0xE45B5B);
    }
}

static void refresh_event(lv_event_t *event)
{
    if (NULL != event && LV_EVENT_CLICKED == lv_event_get_code(event)) {
        request_scan();
    }
}

static void close_keyboard(bool accept)
{
    if (NULL == s_keyboard_page || NULL == s_config_page) return;
    if (accept && NULL != s_keyboard_textarea && NULL != s_password_textarea) {
        lv_textarea_set_text(s_password_textarea,
                             lv_textarea_get_text(s_keyboard_textarea));
    }
    lv_obj_add_flag(s_keyboard_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_config_page, LV_OBJ_FLAG_HIDDEN);
    holocubic_ui_close_keyboard(s_state);
}

static void show_config_page(void)
{
    if (NULL != s_config_page) {
        lv_obj_clear_flag(s_config_page, LV_OBJ_FLAG_HIDDEN);
    }
    if (NULL != s_keyboard_page) {
        lv_obj_add_flag(s_keyboard_page, LV_OBJ_FLAG_HIDDEN);
    }
}

static void clear_display_for_fullscreen_page(void)
{
    if (ESP_OK != holocubic_display_domain_lock(1000)) return;
    (void)display_hal_fill_rgb565(0x0000U);
    holocubic_display_domain_unlock();
}

static void clear_display_for_home_page(void)
{
    if (ESP_OK != holocubic_display_domain_lock(1000)) return;
    (void)display_hal_fill_rgb565(0x0000U);
    holocubic_display_domain_unlock();
}

static void keyboard_event(lv_event_t *event)
{
    if (NULL == event) return;
    if (LV_EVENT_READY == lv_event_get_code(event)) close_keyboard(true);
    if (LV_EVENT_CANCEL == lv_event_get_code(event)) close_keyboard(false);
}

static void password_event(lv_event_t *event)
{
    if (NULL != event && LV_EVENT_CLICKED == lv_event_get_code(event) &&
        NULL != s_keyboard_page && NULL != s_keyboard_textarea) {
        lv_textarea_set_text(s_keyboard_textarea,
                             lv_textarea_get_text(s_password_textarea));
        lv_obj_add_flag(s_config_page, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_keyboard_page, LV_OBJ_FLAG_HIDDEN);
        holocubic_ui_open_keyboard(s_state);
        lv_textarea_set_cursor_pos(s_keyboard_textarea, LV_TEXTAREA_CURSOR_LAST);
    }
}

static void connect_event(lv_event_t *event)
{
    uint16_t selected = 0U;
    network_manager_wifi_config_t config = {0};
    uint32_t operation_id = 0U;

    if (NULL == event || LV_EVENT_CLICKED != lv_event_get_code(event) ||
        0U == s_scan_list.count) return;
    selected = lv_dropdown_get_selected(s_ssid_dropdown);
    if ((size_t)selected >= s_scan_list.count) return;
    config.ssid_len = s_scan_list.entries[selected].ssid_len;
    memcpy(config.ssid, s_scan_list.entries[selected].ssid, config.ssid_len);
    const char *password = lv_textarea_get_text(s_password_textarea);
    const size_t password_len = strlen(password);
    if (password_len > NETWORK_MANAGER_WIFI_PASSWORD_MAX_BYTES) {
        set_status("PASSWORD TOO LONG", 0xE45B5B);
        return;
    }
    config.password_len = (uint8_t)password_len;
    memcpy(config.password, password, password_len);
    if (ESP_OK != network_manager_wifi_set_config(&config, true,
                                                  &operation_id)) {
        set_status("SAVE FAILED", 0xE45B5B);
        return;
    }
    holocubic_ui_connect_submitted(s_state);
    set_status("CONNECTING...", 0x58C7D8);
}

static esp_err_t create_pages(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_t *label = NULL;
    lv_obj_t *button_label = NULL;

    style_panel(screen, color_hex(0x0B1115));
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_14, 0);
    s_config_page = lv_obj_create(screen);
    lv_obj_set_size(s_config_page, HOLO_WIFI_UI_WIDTH, HOLO_WIFI_UI_HEIGHT);
    style_panel(s_config_page, color_hex(0x0B1115));
    label = make_label(s_config_page, 16, 12, 200, 24, "WIFI SETUP");
    lv_obj_set_style_text_color(label, color_hex(0x58C7D8), 0);
    label = make_label(s_config_page, 16, 42, 120, 20, "NETWORK");
    lv_obj_set_style_text_color(label, color_hex(0x8B9BA5), 0);
    s_ssid_dropdown = lv_dropdown_create(s_config_page);
    lv_obj_set_pos(s_ssid_dropdown, 16, 64);
    lv_obj_set_size(s_ssid_dropdown, 252, 40);
    lv_dropdown_set_options_static(s_ssid_dropdown, "PRESS REFRESH");
    lv_obj_set_style_text_font(s_ssid_dropdown, &lv_font_montserrat_14, 0);
    lv_obj_set_style_bg_color(s_ssid_dropdown, color_hex(0x15232A), 0);
    lv_obj_set_style_text_color(s_ssid_dropdown, color_hex(0xE7EEF2), 0);
    s_refresh_button = lv_btn_create(s_config_page);
    lv_obj_set_pos(s_refresh_button, 278, 64);
    lv_obj_set_size(s_refresh_button, 42, 40);
    lv_obj_set_style_bg_color(s_refresh_button, color_hex(0x1D5360), 0);
    lv_obj_add_event_cb(s_refresh_button, refresh_event, LV_EVENT_CLICKED, NULL);
    button_label = lv_label_create(s_refresh_button);
    lv_label_set_text(button_label, LV_SYMBOL_REFRESH);
    lv_obj_center(button_label);
    label = make_label(s_config_page, 16, 116, 120, 20, "PASSWORD");
    lv_obj_set_style_text_color(label, color_hex(0x8B9BA5), 0);
    s_password_textarea = lv_textarea_create(s_config_page);
    lv_obj_set_pos(s_password_textarea, 16, 138);
    lv_obj_set_size(s_password_textarea, 304, 40);
    lv_textarea_set_one_line(s_password_textarea, true);
    lv_textarea_set_password_mode(s_password_textarea, true);
    lv_textarea_set_placeholder_text(s_password_textarea, "tap to type");
    lv_obj_set_style_text_font(s_password_textarea, &lv_font_montserrat_14, 0);
    lv_obj_add_event_cb(s_password_textarea, password_event, LV_EVENT_CLICKED,
                        NULL);
    lv_obj_t *connect_button = lv_btn_create(s_config_page);
    lv_obj_set_pos(connect_button, 16, 190);
    lv_obj_set_size(connect_button, 304, 38);
    lv_obj_set_style_bg_color(connect_button, color_hex(0x1B7F68), 0);
    lv_obj_add_event_cb(connect_button, connect_event, LV_EVENT_CLICKED, NULL);
    button_label = lv_label_create(connect_button);
    lv_label_set_text(button_label, "SAVE AND CONNECT");
    lv_obj_center(button_label);
    s_status_label = make_label(s_config_page, 16, 178, 304, 12, "");
    s_keyboard_page = lv_obj_create(screen);
    lv_obj_set_size(s_keyboard_page, HOLO_WIFI_UI_WIDTH, HOLO_WIFI_UI_HEIGHT);
    style_panel(s_keyboard_page, color_hex(0x10181D));
    lv_obj_add_flag(s_keyboard_page, LV_OBJ_FLAG_HIDDEN);
    s_keyboard_textarea = lv_textarea_create(s_keyboard_page);
    lv_obj_set_pos(s_keyboard_textarea, 16, 12);
    lv_obj_set_size(s_keyboard_textarea, 288, 38);
    lv_textarea_set_one_line(s_keyboard_textarea, true);
    lv_textarea_set_password_mode(s_keyboard_textarea, true);
    s_keyboard = lv_keyboard_create(s_keyboard_page);
    lv_obj_set_pos(s_keyboard, 0, 56);
    lv_obj_set_size(s_keyboard, HOLO_WIFI_UI_WIDTH, 184);
    lv_keyboard_set_textarea(s_keyboard, s_keyboard_textarea);
    lv_obj_add_event_cb(s_keyboard, keyboard_event, LV_EVENT_ALL, NULL);
    if (NULL != s_state && s_state->mode == HOLO_DISPLAY_MAIN) {
        lv_obj_add_flag(s_config_page, LV_OBJ_FLAG_HIDDEN);
    } else {
        request_scan();
    }
    return ESP_OK;
}

static esp_err_t initialize_lvgl(void)
{
    static lv_disp_draw_buf_t draw_buffer;
    static lv_disp_drv_t display_driver;
    static lv_indev_drv_t input_driver;
    const holocubic_wifi_buffer_policy_t policy =
        holocubic_wifi_buffer_policy_default();
    holocubic_wifi_buffer_plan_t preferred_plan = {0};
    holocubic_wifi_buffer_plan_t fallback_plan = {0};
    holocubic_wifi_buffer_plan_t plan = {0};
    const uint32_t dma_caps = MALLOC_CAP_DMA | MALLOC_CAP_8BIT;
    size_t pixels = 0U;

    lv_init();
    if (!holocubic_wifi_buffer_plan_mode(&policy, true, &preferred_plan) ||
        !holocubic_wifi_buffer_plan_mode(&policy, false, &fallback_plan)) {
        return ESP_ERR_INVALID_STATE;
    }
    s_draw_buffer_1 = heap_caps_malloc(preferred_plan.bytes_per_buffer,
                                       dma_caps);
    s_draw_buffer_2 = heap_caps_malloc(preferred_plan.bytes_per_buffer,
                                       dma_caps);
    if (NULL != s_draw_buffer_1 && NULL != s_draw_buffer_2) {
        plan = preferred_plan;
    } else {
        heap_caps_free(s_draw_buffer_1);
        heap_caps_free(s_draw_buffer_2);
        s_draw_buffer_1 = NULL;
        s_draw_buffer_2 = NULL;
        plan = fallback_plan;
        s_draw_buffer_1 = heap_caps_malloc(fallback_plan.bytes_per_buffer,
                                           dma_caps);
        if (NULL == s_draw_buffer_1) {
            heap_caps_free(s_draw_buffer_1);
            s_draw_buffer_1 = NULL;
            ESP_LOGE(TAG,
                     "Wi-Fi UI fallback DMA buffer unavailable: bytes=%u free=%u largest=%u min=%u",
                     (unsigned)plan.bytes_per_buffer,
                     (unsigned)heap_caps_get_free_size(dma_caps),
                     (unsigned)heap_caps_get_largest_free_block(dma_caps),
                     (unsigned)heap_caps_get_minimum_free_size(dma_caps));
            return ESP_ERR_NO_MEM;
        }
    }
    pixels = plan.pixels;
    ESP_LOGI(TAG, "LVGL full-screen buffers: lines=%u bytes=%u mode=%s",
             (unsigned)plan.lines, (unsigned)plan.bytes_per_buffer,
             plan.double_buffer ? "double" : "single-fallback");
    lv_disp_draw_buf_init(&draw_buffer, s_draw_buffer_1,
                          plan.double_buffer ? s_draw_buffer_2 : NULL, pixels);
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = HOLO_WIFI_UI_WIDTH;
    display_driver.ver_res = HOLO_WIFI_UI_HEIGHT;
    display_driver.flush_cb = flush_callback;
    display_driver.draw_buf = &draw_buffer;
    lv_disp_drv_register(&display_driver);
    lv_indev_drv_init(&input_driver);
    input_driver.type = LV_INDEV_TYPE_POINTER;
    input_driver.read_cb = touch_read_callback;
    lv_indev_drv_register(&input_driver);
    return create_pages();
}

static void ui_task(void *argument)
{
    holocubic_ui_clock_t clock = {0};

    (void)argument;
    if (ESP_OK != initialize_lvgl()) {
        ESP_LOGE(TAG, "LVGL full-screen setup failed");
        vTaskDelete(NULL);
        return;
    }
    holocubic_ui_clock_init(&clock);
    s_ready = true;
    while (true) {
        network_manager_snapshot_t snapshot = {0};
        const uint32_t elapsed_ms = holocubic_ui_clock_advance(
            &clock, (uint64_t)esp_timer_get_time() / 1000ULL);

        if (0U < elapsed_ms) {
            lv_tick_inc(elapsed_ms);
        }
        if (s_open_requested) {
            s_open_requested = false;
            holocubic_ui_open_wifi(s_state);
            clear_display_for_fullscreen_page();
            show_config_page();
            request_scan();
        }
        if (s_scan_pending && ESP_OK == network_manager_wifi_scan_get_latest(
                &s_scan_list)) {
            s_scan_pending = false;
            render_scan_list();
            set_status(ESP_OK == s_scan_list.result ? "SELECT NETWORK" :
                       "SCAN FAILED", ESP_OK == s_scan_list.result ?
                       0x8B9BA5 : 0xE45B5B);
        }
        if (NULL != s_state && s_state->connect_pending &&
            ESP_OK == network_manager_get_snapshot(&snapshot) &&
            snapshot.stable_ready &&
            snapshot.stable_active_interface == NETWORK_MANAGER_INTERFACE_WIFI) {
            holocubic_ui_wifi_ready(s_state);
            lv_obj_add_flag(s_config_page, LV_OBJ_FLAG_HIDDEN);
            if (NULL != s_keyboard_page) {
                lv_obj_add_flag(s_keyboard_page, LV_OBJ_FLAG_HIDDEN);
            }
            lv_obj_invalidate(lv_scr_act());
            lv_refr_now(NULL);
            clear_display_for_home_page();
            holocubic_ui_home_handoff_complete(s_state);
            ESP_LOGI(TAG, "Wi-Fi connected; home display handoff complete");
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(HOLO_WIFI_UI_TASK_DELAY_MS));
    }
}

esp_err_t holocubic_wifi_ui_start(holocubic_ui_state_t *state)
{
    if (NULL == state) return ESP_ERR_INVALID_ARG;
    s_state = state;
    s_scan_list = (network_manager_wifi_scan_list_t){0};
    if (pdPASS != xTaskCreate(ui_task, "holo_wifi_ui", 8192U, NULL, 4U, NULL)) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

bool holocubic_wifi_ui_is_active(void)
{
    return NULL != s_state && !holocubic_ui_main_input_enabled(s_state);
}

void holocubic_wifi_ui_open(void)
{
    if (NULL == s_state || !s_ready) return;
    holocubic_ui_open_wifi(s_state);
    s_open_requested = true;
}
