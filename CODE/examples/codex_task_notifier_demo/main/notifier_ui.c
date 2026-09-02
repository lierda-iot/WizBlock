#include "notifier_ui.h"

#include "notifier_font.h"

#include "board_laiwfs300.h"
#include "board_pins.h"
#include "display_hal.h"
#include "touch_hal.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <limits.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "notifier_ui";

#define UI_HOR_RES                 BOARD_LAIWFS300_LCD_V_RES
#define UI_VER_RES                 BOARD_LAIWFS300_LCD_H_RES
#define UI_LCD_PCLK_HZ             20000000U
#define UI_BUFFER_ROWS             20U
#define UI_TICK_MS                 2U
#define UI_TASK_DELAY_MS           10U
#define UI_MODEL_REFRESH_MS        100U
#define UI_TASK_STACK              9216U
#define UI_TASK_PRIORITY           2U
#define UI_TASK_CORE               1U
#define UI_INIT_TIMEOUT_MS         10000U
#define UI_FLUSH_TIMEOUT_MS        1000
#define UI_TOUCH_INIT_RETRY_COUNT  3U
#define UI_TOUCH_INIT_RETRY_MS     150U
#define UI_HEADER_HEIGHT           22
#define UI_TASK_AREA_Y             24
#define UI_CARD_MARGIN             4
#define UI_CARD_GAP                4
#define UI_CARD_WIDTH              154
#define UI_CARD_HEIGHT             98
#define UI_CARD_RADIUS             6
#define UI_CARD_TITLE_X            8
#define UI_CARD_TITLE_Y            7
#define UI_CARD_TITLE_WIDTH        138
#define UI_CARD_TITLE_HEIGHT       42
#define UI_CARD_CLIENT_Y           55
#define UI_CARD_STATUS_Y           77
#define UI_CARD_TIME_X             66
#define UI_CARD_TIME_WIDTH         80
#define UI_FOOTER_HEIGHT           16
#define UI_FOOTER_Y                (UI_VER_RES - UI_FOOTER_HEIGHT)
#define UI_ALERT_FRAME_MS          150U
#define UI_ALERT_PROGRESS_X        24
#define UI_ALERT_PROGRESS_Y        184
#define UI_ALERT_PROGRESS_WIDTH    272
#define UI_ALERT_PROGRESS_HEIGHT   6
#define UI_KEYBOARD_HEADER_HEIGHT  56
#define UI_KEYBOARD_HEIGHT         (UI_VER_RES - UI_KEYBOARD_HEADER_HEIGHT)
#define UI_WIFI_OPTIONS_BUFFER_BYTES                                      \
    ((NOTIFIER_WIFI_SCAN_MAX_NETWORKS *                                  \
      (NOTIFIER_WIFI_SSID_MAX_BYTES + 1U)) +                             \
     1U)

#define UI_TITLE_FONT              (&g_notifier_font_noto_sans_sc_16)

#if LV_FONT_MONTSERRAT_28
#define UI_ALERT_HEADING_FONT      (&lv_font_montserrat_28)
#else
#define UI_ALERT_HEADING_FONT      (&lv_font_montserrat_14)
#endif

typedef struct {
    lv_obj_t *background;
    lv_obj_t *title;
    lv_obj_t *client;
    lv_obj_t *status;
    lv_obj_t *elapsed;
} ui_card_t;

typedef struct {
    notifier_wifi_scan_list_t networks;
    esp_err_t scan_result;
} ui_wifi_scan_update_t;

_Static_assert((2 * UI_CARD_MARGIN) + (2 * UI_CARD_WIDTH) + UI_CARD_GAP ==
                   UI_HOR_RES,
               "card columns must fill the display width");
_Static_assert(UI_TASK_AREA_Y + (2 * UI_CARD_HEIGHT) + UI_CARD_GAP +
                       UI_FOOTER_HEIGHT ==
                   UI_VER_RES,
               "card rows and footer must fill the display height");

static notifier_ui_copy_model_fn s_copy_model;
static void *s_copy_model_context;
static notifier_ui_save_wifi_fn s_save_wifi;
static notifier_ui_request_wifi_scan_fn s_request_wifi_scan;
static void *s_save_wifi_context;
static SemaphoreHandle_t s_init_semaphore;
static QueueHandle_t s_wifi_scan_update_queue;
static esp_err_t s_init_result = ESP_FAIL;
static esp_timer_handle_t s_tick_timer;
static lv_color_t *s_draw_buffer_1;
static lv_color_t *s_draw_buffer_2;
static volatile uint32_t s_flush_errors;
static notifier_model_t s_ui_model;
static notifier_wifi_credentials_t s_saved_wifi_credentials;
static notifier_wifi_scan_list_t s_wifi_scan_list;
static bool s_has_saved_wifi_credentials;

static lv_obj_t *s_run_label;
static lv_obj_t *s_done_label;
static lv_obj_t *s_connection_label;
static lv_obj_t *s_page_label;
static lv_obj_t *s_overflow_label;
static lv_obj_t *s_settings_page;
static lv_obj_t *s_keyboard_page;
static lv_obj_t *s_ssid_dropdown;
static lv_obj_t *s_password_textarea;
static lv_obj_t *s_settings_message_label;
static lv_obj_t *s_cancel_button;
static lv_obj_t *s_refresh_button;
static lv_obj_t *s_save_button;
static lv_obj_t *s_keyboard;
static lv_obj_t *s_keyboard_textarea;
static lv_obj_t *s_keyboard_title_label;
static lv_obj_t *s_keyboard_target;
static lv_obj_t *s_alert_overlay;
static lv_obj_t *s_alert_heading;
static lv_obj_t *s_alert_title;
static lv_obj_t *s_alert_progress;
static uint32_t s_rendered_alert_generation;
static bool s_alert_visible;
static ui_card_t s_cards[NOTIFIER_TASKS_PER_PAGE];

static void settings_button_event_callback(lv_event_t *event);
static void settings_cancel_event_callback(lv_event_t *event);
static void settings_save_event_callback(lv_event_t *event);
static void settings_textarea_event_callback(lv_event_t *event);
static void settings_dropdown_event_callback(lv_event_t *event);
static void settings_refresh_event_callback(lv_event_t *event);
static void keyboard_event_callback(lv_event_t *event);
static void keyboard_back_event_callback(lv_event_t *event);
static void keyboard_done_event_callback(lv_event_t *event);
static esp_err_t create_wifi_pages(lv_obj_t *screen);

static lv_color_t color_hex(uint32_t rgb)
{
    return lv_color_hex(rgb);
}

static void style_panel(lv_obj_t *object, lv_color_t background)
{
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(object, background, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_label(lv_obj_t *parent, int x, int y, int width,
                              int height, const lv_font_t *font,
                              lv_text_align_t alignment)
{
    lv_obj_t *label = lv_label_create(parent);

    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, alignment, 0);
    lv_obj_set_style_text_color(label, color_hex(0xE7ECEF), 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    return label;
}

static lv_color_t card_background(notifier_task_status_t status, bool online)
{
    switch (status) {
        case NOTIFIER_TASK_RUN:
            return color_hex(online ? 0xF2C94C : 0x66582A);
        case NOTIFIER_TASK_DONE:
            return color_hex(online ? 0x42B86B : 0x285C3B);
        case NOTIFIER_TASK_STOP:
            return color_hex(online ? 0x737A80 : 0x41474B);
        case NOTIFIER_TASK_UNKNOWN:
        default:
            return color_hex(online ? 0xD95055 : 0x6A3437);
    }
}

static lv_color_t card_primary_text(notifier_task_status_t status, bool online)
{
    if (!online) {
        return color_hex(0xC2C6C8);
    }
    if (NOTIFIER_TASK_RUN == status || NOTIFIER_TASK_DONE == status) {
        return color_hex(0x172019);
    }
    return color_hex(0xFFFFFF);
}

static lv_color_t card_secondary_text(notifier_task_status_t status,
                                      bool online)
{
    if (!online) {
        return color_hex(0xA5AAAD);
    }
    if (NOTIFIER_TASK_RUN == status || NOTIFIER_TASK_DONE == status) {
        return color_hex(0x29332C);
    }
    return color_hex(0xF0F2F3);
}

static void format_elapsed(uint32_t elapsed_ms, char *text, size_t text_size)
{
    uint32_t seconds = elapsed_ms / 1000U;

    if (NULL == text || 0U == text_size) {
        return;
    }
    if (seconds < 60U) {
        snprintf(text, text_size, "%lus", (unsigned long)seconds);
    } else if (seconds < 3600U) {
        snprintf(text, text_size, "%lum%02lu",
                 (unsigned long)(seconds / 60U),
                 (unsigned long)(seconds % 60U));
    } else {
        snprintf(text, text_size, "%luh%02lu",
                 (unsigned long)(seconds / 3600U),
                 (unsigned long)((seconds / 60U) % 60U));
    }
}

static void flush_callback(lv_disp_drv_t *driver, const lv_area_t *area,
                           lv_color_t *color_map)
{
    uint32_t pixel_count = (uint32_t)(area->x2 - area->x1 + 1) *
                           (uint32_t)(area->y2 - area->y1 + 1);
    uint16_t *pixels = (uint16_t *)color_map;
    esp_err_t result = ESP_OK;

    for (uint32_t index = 0U; index < pixel_count; ++index) {
        uint16_t pixel = pixels[index];
        uint16_t red = (pixel >> 11U) & 0x1FU;
        uint16_t green = (pixel >> 5U) & 0x3FU;
        uint16_t blue = pixel & 0x1FU;
        uint16_t bgr = (uint16_t)((blue << 11U) | (green << 5U) | red);
        pixels[index] = (uint16_t)((bgr >> 8U) | (bgr << 8U));
    }

    result = display_hal_draw_bitmap_rgb565(area->x1, area->y1,
                                             area->x2 - area->x1 + 1,
                                             area->y2 - area->y1 + 1,
                                             pixels);
    if (ESP_OK == result) {
        result = display_hal_wait_pending(UI_FLUSH_TIMEOUT_MS);
    }
    if (ESP_OK != result && UINT32_MAX != s_flush_errors) {
        s_flush_errors++;
        if (s_flush_errors <= 3U) {
            ESP_LOGE(TAG, "LCD flush failed: %s", esp_err_to_name(result));
        }
    }
    lv_disp_flush_ready(driver);
}

static void tick_callback(void *argument)
{
    (void)argument;
    lv_tick_inc(UI_TICK_MS);
}

static void touch_read_callback(lv_indev_drv_t *driver,
                                lv_indev_data_t *data)
{
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;
    touch_panel_point_t point = {0};
    uint8_t count = 0U;
    esp_err_t result = ESP_OK;

    (void)driver;
    if (NULL == data) {
        return;
    }
    result = touch_panel_read_point(&point, &count);
    if (ESP_OK == result && 0U < count && count <= 2U) {
        last_x = (lv_coord_t)(UI_HOR_RES - 1 - point.y);
        last_y = (lv_coord_t)point.x;
        data->state = LV_INDEV_STATE_PR;
    } else {
        data->state = LV_INDEV_STATE_REL;
    }
    data->point.x = last_x;
    data->point.y = last_y;
}

static esp_err_t create_screen(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_t *header = NULL;
    lv_obj_t *title_label = NULL;
    lv_obj_t *settings_button = NULL;
    lv_obj_t *settings_icon = NULL;
    lv_obj_t *footer = NULL;
    lv_obj_t *progress_track = NULL;

    style_panel(screen, color_hex(0x111315));
    lv_obj_set_style_text_font(screen, &lv_font_montserrat_12, 0);

    header = lv_obj_create(screen);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, UI_HOR_RES, UI_HEADER_HEIGHT);
    style_panel(header, color_hex(0x23282C));
    title_label = create_label(header, 6, 2, 52, 18,
                               &lv_font_montserrat_14, LV_TEXT_ALIGN_LEFT);
    s_run_label = create_label(header, 62, 4, 52, 16,
                               &lv_font_montserrat_12, LV_TEXT_ALIGN_LEFT);
    s_done_label = create_label(header, 118, 4, 66, 16,
                                &lv_font_montserrat_12, LV_TEXT_ALIGN_LEFT);
    s_connection_label = create_label(header, 194, 4, 92, 16,
                                      &lv_font_montserrat_12, LV_TEXT_ALIGN_RIGHT);
    lv_label_set_long_mode(title_label, LV_LABEL_LONG_CLIP);
    lv_label_set_text(title_label, "CODEX");
    lv_label_set_text(s_run_label, "RUN 0");
    lv_label_set_text(s_done_label, "DONE 0");
    lv_label_set_text(s_connection_label, "OFFLINE");

    settings_button = lv_btn_create(header);
    lv_obj_set_pos(settings_button, 292, 1);
    lv_obj_set_size(settings_button, 24, 20);
    lv_obj_set_style_radius(settings_button, 4, 0);
    lv_obj_set_style_bg_color(settings_button, color_hex(0x3A4248), 0);
    lv_obj_set_style_bg_color(settings_button, color_hex(0x59636A),
                              LV_STATE_PRESSED);
    lv_obj_set_style_pad_all(settings_button, 0, 0);
    lv_obj_add_event_cb(settings_button, settings_button_event_callback,
                        LV_EVENT_CLICKED, NULL);
    settings_icon = lv_label_create(settings_button);
    lv_obj_set_style_text_font(settings_icon, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(settings_icon, color_hex(0xE7ECEF), 0);
    lv_label_set_text(settings_icon, LV_SYMBOL_SETTINGS);
    lv_obj_center(settings_icon);

    for (uint8_t index = 0U; index < NOTIFIER_TASKS_PER_PAGE; ++index) {
        int card_x = UI_CARD_MARGIN +
                     ((int)(index % 2U) * (UI_CARD_WIDTH + UI_CARD_GAP));
        int card_y = UI_TASK_AREA_Y +
                     ((int)(index / 2U) * (UI_CARD_HEIGHT + UI_CARD_GAP));
        ui_card_t *card = &s_cards[index];

        card->background = lv_obj_create(screen);
        lv_obj_set_pos(card->background, card_x, card_y);
        lv_obj_set_size(card->background, UI_CARD_WIDTH, UI_CARD_HEIGHT);
        style_panel(card->background, color_hex(0x1B1F22));
        lv_obj_set_style_radius(card->background, UI_CARD_RADIUS, 0);
        lv_obj_set_style_border_width(card->background, 1, 0);
        lv_obj_set_style_border_color(card->background, color_hex(0x31373B), 0);
        lv_obj_set_style_outline_width(card->background, 0, 0);
        lv_obj_set_style_outline_pad(card->background, 0, 0);
        lv_obj_set_style_outline_color(card->background, color_hex(0xFFFFFF), 0);
        card->title = create_label(
            card->background, UI_CARD_TITLE_X, UI_CARD_TITLE_Y,
            UI_CARD_TITLE_WIDTH, UI_CARD_TITLE_HEIGHT,
            UI_TITLE_FONT, LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(card->title, LV_LABEL_LONG_DOT);
        card->client = create_label(
            card->background, UI_CARD_TITLE_X, UI_CARD_CLIENT_Y,
            UI_CARD_TITLE_WIDTH, 16,
            &lv_font_montserrat_12, LV_TEXT_ALIGN_LEFT);
        lv_label_set_long_mode(card->client, LV_LABEL_LONG_CLIP);
        card->status = create_label(
            card->background, UI_CARD_TITLE_X, UI_CARD_STATUS_Y,
            54, 14, &lv_font_montserrat_10, LV_TEXT_ALIGN_LEFT);
        card->elapsed = create_label(
            card->background, UI_CARD_TIME_X, UI_CARD_STATUS_Y - 2,
            UI_CARD_TIME_WIDTH, 16,
            &lv_font_montserrat_12, LV_TEXT_ALIGN_RIGHT);
    }

    footer = lv_obj_create(screen);
    lv_obj_set_pos(footer, 0, UI_FOOTER_Y);
    lv_obj_set_size(footer, UI_HOR_RES, UI_FOOTER_HEIGHT);
    style_panel(footer, color_hex(0x23282C));
    s_page_label = create_label(footer, 6, 1, 120, 14,
                                &lv_font_montserrat_10, LV_TEXT_ALIGN_LEFT);
    s_overflow_label = create_label(footer, 226, 1, 88, 14,
                                    &lv_font_montserrat_10, LV_TEXT_ALIGN_RIGHT);
    lv_label_set_text(s_page_label, "PAGE 1/1");
    lv_label_set_text(s_overflow_label, "");

    s_alert_overlay = lv_obj_create(screen);
    lv_obj_set_pos(s_alert_overlay, 0, 0);
    lv_obj_set_size(s_alert_overlay, UI_HOR_RES, UI_VER_RES);
    style_panel(s_alert_overlay, color_hex(0x123B2B));
    lv_obj_add_flag(s_alert_overlay, LV_OBJ_FLAG_HIDDEN);
    s_alert_heading = create_label(s_alert_overlay, 16, 68, 288, 38,
                                   UI_ALERT_HEADING_FONT,
                                   LV_TEXT_ALIGN_CENTER);
    lv_label_set_text(s_alert_heading, "TASK DONE");
    lv_obj_set_style_text_color(s_alert_heading, color_hex(0xF3FFF8), 0);
    s_alert_title = create_label(s_alert_overlay, 20, 116, 280, 24,
                                 UI_TITLE_FONT, LV_TEXT_ALIGN_CENTER);
    lv_label_set_long_mode(s_alert_title, LV_LABEL_LONG_DOT);
    progress_track = lv_obj_create(s_alert_overlay);
    lv_obj_set_pos(progress_track, UI_ALERT_PROGRESS_X, UI_ALERT_PROGRESS_Y);
    lv_obj_set_size(progress_track, UI_ALERT_PROGRESS_WIDTH,
                    UI_ALERT_PROGRESS_HEIGHT);
    style_panel(progress_track, color_hex(0x0B251B));
    s_alert_progress = lv_obj_create(progress_track);
    lv_obj_set_pos(s_alert_progress, 0, 0);
    lv_obj_set_size(s_alert_progress, 0, UI_ALERT_PROGRESS_HEIGHT);
    style_panel(s_alert_progress, color_hex(0x70E0A3));
    return create_wifi_pages(screen);
}

static const char *wifi_validation_message(
    notifier_wifi_config_result_t result)
{
    switch (result) {
        case NOTIFIER_WIFI_CONFIG_SSID_REQUIRED:
            return "请输入 Wi-Fi 名称";
        case NOTIFIER_WIFI_CONFIG_SSID_TOO_LONG:
            return "名称不能超过 32 字节";
        case NOTIFIER_WIFI_CONFIG_PASSWORD_LENGTH:
            return "密码需为空或 8-63 位";
        case NOTIFIER_WIFI_CONFIG_PASSWORD_CHARACTER:
            return "密码含不可用字符";
        case NOTIFIER_WIFI_CONFIG_INVALID_ARGUMENT:
        case NOTIFIER_WIFI_CONFIG_OK:
        default:
            return "配置无效";
    }
}

static lv_obj_t *create_settings_textarea(lv_obj_t *parent, int y,
                                           const char *placeholder,
                                           bool password_mode)
{
    lv_obj_t *textarea = lv_textarea_create(parent);

    lv_obj_set_pos(textarea, 88, y);
    lv_obj_set_size(textarea, 220, 38);
    lv_textarea_set_one_line(textarea, true);
    lv_textarea_set_max_length(
        textarea, password_mode ? NOTIFIER_WIFI_PASSWORD_MAX_BYTES
                                : NOTIFIER_WIFI_SSID_MAX_BYTES);
    lv_textarea_set_placeholder_text(textarea, placeholder);
    lv_textarea_set_password_mode(textarea, password_mode);
    lv_obj_set_style_text_font(textarea, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(textarea, color_hex(0x172019), 0);
    lv_obj_set_style_bg_color(textarea, color_hex(0xF4F7F8), 0);
    lv_obj_set_style_border_width(textarea, 1, 0);
    lv_obj_set_style_border_color(textarea, color_hex(0x87939A), 0);
    lv_obj_set_style_radius(textarea, 6, 0);
    lv_obj_add_event_cb(textarea, settings_textarea_event_callback,
                        LV_EVENT_CLICKED, NULL);
    return textarea;
}

static void set_control_enabled(lv_obj_t *control, bool enabled)
{
    if (NULL == control) {
        return;
    }
    if (enabled) {
        lv_obj_clear_state(control, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(control, LV_STATE_DISABLED);
    }
}

static bool update_wifi_dropdown_options(void)
{
    char options[UI_WIFI_OPTIONS_BUFFER_BYTES] = {0};
    size_t offset = 0U;
    size_t index = 0U;

    if (NULL == s_ssid_dropdown || 0U == s_wifi_scan_list.count) {
        return false;
    }
    for (index = 0U; index < s_wifi_scan_list.count; index++) {
        const char *ssid = s_wifi_scan_list.networks[index].ssid;
        size_t ssid_length = strlen(ssid);

        if (0U < index) {
            if (offset + 1U >= sizeof(options)) {
                return false;
            }
            options[offset++] = '\n';
        }
        if (ssid_length >= sizeof(options) - offset) {
            return false;
        }
        memcpy(&options[offset], ssid, ssid_length);
        offset += ssid_length;
    }
    options[offset] = '\0';
    lv_dropdown_set_options(s_ssid_dropdown, options);
    lv_dropdown_set_selected(s_ssid_dropdown,
                             (uint16_t)s_wifi_scan_list.selected_index);
    return true;
}

static void sync_password_for_selected_network(void)
{
    uint16_t selected = 0U;

    if (NULL == s_ssid_dropdown || NULL == s_password_textarea ||
        0U == s_wifi_scan_list.count) {
        return;
    }
    selected = lv_dropdown_get_selected(s_ssid_dropdown);
    if ((size_t)selected >= s_wifi_scan_list.count) {
        lv_textarea_set_text(s_password_textarea, "");
        return;
    }
    if (s_has_saved_wifi_credentials &&
        0 == strcmp(s_wifi_scan_list.networks[selected].ssid,
                    s_saved_wifi_credentials.ssid)) {
        lv_textarea_set_text(s_password_textarea,
                             s_saved_wifi_credentials.password);
    } else {
        lv_textarea_set_text(s_password_textarea, "");
    }
}

static void render_wifi_scan_update(const ui_wifi_scan_update_t *update)
{
    char message[48] = {0};
    size_t scanned_count = 0U;
    bool has_selectable_network = false;

    if (NULL == update || NULL == s_settings_message_label) {
        return;
    }
    scanned_count = update->networks.count;
    s_wifi_scan_list = update->networks;
    notifier_wifi_scan_list_finalize(
        &s_wifi_scan_list,
        s_has_saved_wifi_credentials ? s_saved_wifi_credentials.ssid : NULL);
    has_selectable_network = update_wifi_dropdown_options();

    set_control_enabled(s_refresh_button, true);
    set_control_enabled(s_ssid_dropdown, has_selectable_network);
    set_control_enabled(s_password_textarea, has_selectable_network);
    set_control_enabled(s_save_button, has_selectable_network);
    if (has_selectable_network) {
        sync_password_for_selected_network();
    }

    if (ESP_OK != update->scan_result) {
        if (has_selectable_network) {
            lv_label_set_text(s_settings_message_label,
                              "扫描失败，可使用已保存网络");
        } else {
            lv_dropdown_set_options_static(s_ssid_dropdown, "扫描失败");
            lv_label_set_text(s_settings_message_label,
                              "扫描失败，请刷新");
        }
    } else if (0U == scanned_count) {
        if (has_selectable_network) {
            lv_label_set_text(s_settings_message_label,
                              "未发现其他网络");
        } else {
            lv_dropdown_set_options_static(s_ssid_dropdown, "未发现网络");
            lv_label_set_text(s_settings_message_label,
                              "未发现网络，请刷新");
        }
    } else {
        (void)snprintf(message, sizeof(message), "已发现 %u 个网络",
                       (unsigned)scanned_count);
        lv_label_set_text(s_settings_message_label, message);
    }
    ESP_LOGI(TAG, "[wifi] scan view result=%s options=%u saved=%u",
             (ESP_OK == update->scan_result) ? "ok" : "error",
             (unsigned)s_wifi_scan_list.count,
             s_has_saved_wifi_credentials ? 1U : 0U);
}

static void request_wifi_scan(void)
{
    esp_err_t result = ESP_OK;

    if (NULL == s_ssid_dropdown || NULL == s_settings_message_label) {
        return;
    }
    notifier_wifi_scan_list_init(&s_wifi_scan_list);
    lv_dropdown_set_options_static(s_ssid_dropdown, "正在扫描...");
    lv_label_set_text(s_settings_message_label, "正在扫描...");
    set_control_enabled(s_ssid_dropdown, false);
    set_control_enabled(s_password_textarea, false);
    set_control_enabled(s_refresh_button, false);
    set_control_enabled(s_save_button, false);
    if (NULL == s_request_wifi_scan) {
        result = ESP_ERR_INVALID_STATE;
    } else {
        result = s_request_wifi_scan(s_save_wifi_context);
    }
    if (ESP_OK == result || ESP_ERR_INVALID_STATE == result) {
        return;
    }

    ui_wifi_scan_update_t update = {
        .scan_result = result,
    };
    render_wifi_scan_update(&update);
    ESP_LOGE(TAG, "[wifi] scan request failed: %s",
             esp_err_to_name(result));
}

static void close_keyboard(bool apply_text)
{
    lv_obj_t *target = s_keyboard_target;

    if (NULL == s_keyboard || NULL == s_keyboard_page ||
        NULL == s_settings_page) {
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
    lv_obj_move_foreground(s_settings_page);
    if (NULL != target) {
        lv_obj_clear_state(target, LV_STATE_FOCUSED);
    }
}

static void show_settings_page(void)
{
    if (NULL == s_settings_page) {
        return;
    }
    if (s_has_saved_wifi_credentials) {
        lv_textarea_set_text(s_password_textarea,
                             s_saved_wifi_credentials.password);
        lv_obj_clear_flag(s_cancel_button, LV_OBJ_FLAG_HIDDEN);
    } else {
        lv_obj_add_flag(s_cancel_button, LV_OBJ_FLAG_HIDDEN);
    }
    lv_label_set_text(s_settings_message_label, "");
    lv_obj_add_flag(s_keyboard_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_settings_page);
    request_wifi_scan();
}

static void show_keyboard(lv_obj_t *target)
{
    const char *text = NULL;

    if (target != s_password_textarea || NULL == s_keyboard ||
        NULL == s_keyboard_textarea || NULL == s_keyboard_page) {
        return;
    }
    text = lv_textarea_get_text(target);
    s_keyboard_target = target;
    lv_label_set_text(s_keyboard_title_label, "密码");
    lv_textarea_set_password_mode(s_keyboard_textarea, true);
    lv_textarea_set_max_length(s_keyboard_textarea,
                               NOTIFIER_WIFI_PASSWORD_MAX_BYTES);
    lv_textarea_set_text(s_keyboard_textarea,
                         (NULL != text) ? text : "");
    lv_textarea_set_cursor_pos(s_keyboard_textarea,
                               LV_TEXTAREA_CURSOR_LAST);
    lv_keyboard_set_textarea(s_keyboard, s_keyboard_textarea);
    lv_obj_add_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_keyboard_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(s_keyboard_page);
}

static void settings_button_event_callback(lv_event_t *event)
{
    if (NULL != event && LV_EVENT_CLICKED == lv_event_get_code(event)) {
        show_settings_page();
    }
}

static void settings_cancel_event_callback(lv_event_t *event)
{
    if (NULL == event || LV_EVENT_CLICKED != lv_event_get_code(event) ||
        !s_has_saved_wifi_credentials) {
        return;
    }
    lv_obj_add_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
}

static void settings_save_event_callback(lv_event_t *event)
{
    notifier_wifi_credentials_t credentials = {0};
    notifier_wifi_config_result_t validation = NOTIFIER_WIFI_CONFIG_OK;
    const char *ssid = NULL;
    const char *password = NULL;
    uint16_t selected = 0U;
    esp_err_t result = ESP_OK;

    if (NULL == event || LV_EVENT_CLICKED != lv_event_get_code(event)) {
        return;
    }
    if (NULL == s_ssid_dropdown || 0U == s_wifi_scan_list.count) {
        lv_label_set_text(s_settings_message_label, "请先选择网络");
        return;
    }
    selected = lv_dropdown_get_selected(s_ssid_dropdown);
    if ((size_t)selected >= s_wifi_scan_list.count) {
        lv_label_set_text(s_settings_message_label, "网络选择无效");
        return;
    }
    ssid = s_wifi_scan_list.networks[selected].ssid;
    password = lv_textarea_get_text(s_password_textarea);
    validation = notifier_wifi_credentials_set(&credentials, ssid, password);
    if (NOTIFIER_WIFI_CONFIG_OK != validation) {
        lv_label_set_text(s_settings_message_label,
                          wifi_validation_message(validation));
        return;
    }
    if (NULL == s_save_wifi) {
        lv_label_set_text(s_settings_message_label, "保存服务不可用");
        return;
    }
    result = s_save_wifi(&credentials, s_save_wifi_context);
    if (ESP_OK != result) {
        lv_label_set_text(s_settings_message_label, "保存失败，请重试");
        ESP_LOGE(TAG, "[wifi] settings submit failed: %s",
                 esp_err_to_name(result));
        return;
    }
    s_saved_wifi_credentials = credentials;
    s_has_saved_wifi_credentials = true;
    lv_obj_clear_flag(s_cancel_button, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_settings_message_label, "");
    lv_obj_add_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
    ESP_LOGI(TAG, "[wifi] settings submitted");
}

static void settings_textarea_event_callback(lv_event_t *event)
{
    if (NULL != event && LV_EVENT_CLICKED == lv_event_get_code(event)) {
        show_keyboard(lv_event_get_target(event));
    }
}

static void settings_dropdown_event_callback(lv_event_t *event)
{
    lv_event_code_t code = LV_EVENT_ALL;

    if (NULL == event) {
        return;
    }
    code = lv_event_get_code(event);
    if (LV_EVENT_READY == code) {
        lv_obj_t *list = lv_dropdown_get_list(s_ssid_dropdown);

        if (NULL != list) {
            lv_obj_set_style_text_font(list, UI_TITLE_FONT, LV_PART_MAIN);
            lv_obj_set_style_text_font(list, UI_TITLE_FONT,
                                       LV_PART_SELECTED);
        }
    } else if (LV_EVENT_VALUE_CHANGED == code) {
        sync_password_for_selected_network();
    }
}

static void settings_refresh_event_callback(lv_event_t *event)
{
    if (NULL != event && LV_EVENT_CLICKED == lv_event_get_code(event)) {
        request_wifi_scan();
    }
}

static void keyboard_event_callback(lv_event_t *event)
{
    lv_event_code_t code = LV_EVENT_ALL;

    if (NULL == event) {
        return;
    }
    code = lv_event_get_code(event);
    if (LV_EVENT_READY == code) {
        close_keyboard(true);
    } else if (LV_EVENT_CANCEL == code) {
        close_keyboard(false);
    }
}

static void keyboard_back_event_callback(lv_event_t *event)
{
    if (NULL != event && LV_EVENT_CLICKED == lv_event_get_code(event)) {
        close_keyboard(false);
    }
}

static void keyboard_done_event_callback(lv_event_t *event)
{
    if (NULL != event && LV_EVENT_CLICKED == lv_event_get_code(event)) {
        close_keyboard(true);
    }
}

static esp_err_t create_wifi_pages(lv_obj_t *screen)
{
    lv_obj_t *title = NULL;
    lv_obj_t *ssid_label = NULL;
    lv_obj_t *password_label = NULL;
    lv_obj_t *button_label = NULL;
    lv_obj_t *back_button = NULL;
    lv_obj_t *done_button = NULL;

    if (NULL == screen) {
        return ESP_ERR_INVALID_ARG;
    }
    s_settings_page = lv_obj_create(screen);
    lv_obj_set_pos(s_settings_page, 0, 0);
    lv_obj_set_size(s_settings_page, UI_HOR_RES, UI_VER_RES);
    style_panel(s_settings_page, color_hex(0x171A1D));
    title = create_label(s_settings_page, 12, 10, 220, 24,
                         UI_TITLE_FONT, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(title, "Wi-Fi 设置");
    ssid_label = create_label(s_settings_page, 12, 49, 70, 20,
                              UI_TITLE_FONT, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(ssid_label, "网络");
    s_ssid_dropdown = lv_dropdown_create(s_settings_page);
    lv_obj_set_pos(s_ssid_dropdown, 88, 40);
    lv_obj_set_size(s_ssid_dropdown, 176, 38);
    lv_dropdown_set_options_static(s_ssid_dropdown, "正在扫描...");
    lv_dropdown_set_symbol(s_ssid_dropdown, "v");
    lv_obj_set_style_text_font(s_ssid_dropdown, UI_TITLE_FONT, LV_PART_MAIN);
    lv_obj_set_style_text_color(s_ssid_dropdown, color_hex(0x172019),
                                LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ssid_dropdown, color_hex(0xF4F7F8),
                              LV_PART_MAIN);
    lv_obj_set_style_border_width(s_ssid_dropdown, 1, LV_PART_MAIN);
    lv_obj_set_style_border_color(s_ssid_dropdown, color_hex(0x87939A),
                                  LV_PART_MAIN);
    lv_obj_set_style_radius(s_ssid_dropdown, 6, LV_PART_MAIN);
    lv_obj_add_event_cb(s_ssid_dropdown, settings_dropdown_event_callback,
                        LV_EVENT_ALL, NULL);

    s_refresh_button = lv_btn_create(s_settings_page);
    lv_obj_set_pos(s_refresh_button, 270, 40);
    lv_obj_set_size(s_refresh_button, 38, 38);
    lv_obj_set_style_radius(s_refresh_button, 6, 0);
    lv_obj_set_style_bg_color(s_refresh_button, color_hex(0x4A5359), 0);
    lv_obj_add_event_cb(s_refresh_button, settings_refresh_event_callback,
                        LV_EVENT_CLICKED, NULL);
    button_label = lv_label_create(s_refresh_button);
    lv_obj_set_style_text_font(button_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(button_label, color_hex(0xFFFFFF), 0);
    lv_label_set_text(button_label, LV_SYMBOL_REFRESH);
    lv_obj_center(button_label);

    password_label = create_label(s_settings_page, 12, 97, 70, 20,
                                  UI_TITLE_FONT, LV_TEXT_ALIGN_LEFT);
    lv_label_set_text(password_label, "密码");
    s_password_textarea = create_settings_textarea(s_settings_page, 88,
                                                    "可留空", true);
    s_settings_message_label = create_label(
        s_settings_page, 12, 135, 296, 24, UI_TITLE_FONT,
        LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(s_settings_message_label,
                                color_hex(0xF18A8A), 0);
    lv_label_set_text(s_settings_message_label, "");

    s_cancel_button = lv_btn_create(s_settings_page);
    lv_obj_set_pos(s_cancel_button, 12, 174);
    lv_obj_set_size(s_cancel_button, 96, 44);
    lv_obj_set_style_radius(s_cancel_button, 6, 0);
    lv_obj_set_style_bg_color(s_cancel_button, color_hex(0x4A5359), 0);
    lv_obj_add_event_cb(s_cancel_button, settings_cancel_event_callback,
                        LV_EVENT_CLICKED, NULL);
    button_label = lv_label_create(s_cancel_button);
    lv_obj_set_style_text_font(button_label, UI_TITLE_FONT, 0);
    lv_obj_set_style_text_color(button_label, color_hex(0xFFFFFF), 0);
    lv_label_set_text(button_label, "取消");
    lv_obj_center(button_label);

    s_save_button = lv_btn_create(s_settings_page);
    lv_obj_set_pos(s_save_button, 116, 174);
    lv_obj_set_size(s_save_button, 192, 44);
    lv_obj_set_style_radius(s_save_button, 6, 0);
    lv_obj_set_style_bg_color(s_save_button, color_hex(0x23865A), 0);
    lv_obj_set_style_bg_color(s_save_button, color_hex(0x1B6A47),
                              LV_STATE_PRESSED);
    lv_obj_add_event_cb(s_save_button, settings_save_event_callback,
                        LV_EVENT_CLICKED, NULL);
    button_label = lv_label_create(s_save_button);
    lv_obj_set_style_text_font(button_label, UI_TITLE_FONT, 0);
    lv_obj_set_style_text_color(button_label, color_hex(0xFFFFFF), 0);
    lv_label_set_text(button_label, "保存并连接");
    lv_obj_center(button_label);

    s_keyboard_page = lv_obj_create(screen);
    lv_obj_set_pos(s_keyboard_page, 0, 0);
    lv_obj_set_size(s_keyboard_page, UI_HOR_RES, UI_VER_RES);
    style_panel(s_keyboard_page, color_hex(0xE5EBEE));
    lv_obj_add_flag(s_keyboard_page, LV_OBJ_FLAG_HIDDEN);
    back_button = lv_btn_create(s_keyboard_page);
    lv_obj_set_pos(back_button, 6, 8);
    lv_obj_set_size(back_button, 42, 40);
    lv_obj_set_style_radius(back_button, 6, 0);
    lv_obj_set_style_bg_color(back_button, color_hex(0x59646B), 0);
    lv_obj_add_event_cb(back_button, keyboard_back_event_callback,
                        LV_EVENT_CLICKED, NULL);
    button_label = lv_label_create(back_button);
    lv_obj_set_style_text_font(button_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(button_label, color_hex(0xFFFFFF), 0);
    lv_label_set_text(button_label, LV_SYMBOL_LEFT);
    lv_obj_center(button_label);

    s_keyboard_title_label = create_label(
        s_keyboard_page, 52, 18, 82, 22, UI_TITLE_FONT,
        LV_TEXT_ALIGN_CENTER);
    lv_obj_set_style_text_color(s_keyboard_title_label,
                                color_hex(0x263238), 0);
    lv_label_set_text(s_keyboard_title_label, "密码");
    s_keyboard_textarea = lv_textarea_create(s_keyboard_page);
    lv_obj_set_pos(s_keyboard_textarea, 138, 8);
    lv_obj_set_size(s_keyboard_textarea, 126, 40);
    lv_textarea_set_one_line(s_keyboard_textarea, true);
    lv_obj_set_style_text_font(s_keyboard_textarea,
                               &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(s_keyboard_textarea,
                                color_hex(0x172019), 0);
    lv_obj_set_style_bg_color(s_keyboard_textarea,
                              color_hex(0xFFFFFF), 0);
    lv_obj_set_style_border_width(s_keyboard_textarea, 1, 0);
    lv_obj_set_style_border_color(s_keyboard_textarea,
                                  color_hex(0x87939A), 0);
    lv_obj_set_style_radius(s_keyboard_textarea, 6, 0);

    done_button = lv_btn_create(s_keyboard_page);
    lv_obj_set_pos(done_button, 272, 8);
    lv_obj_set_size(done_button, 42, 40);
    lv_obj_set_style_radius(done_button, 6, 0);
    lv_obj_set_style_bg_color(done_button, color_hex(0x23865A), 0);
    lv_obj_add_event_cb(done_button, keyboard_done_event_callback,
                        LV_EVENT_CLICKED, NULL);
    button_label = lv_label_create(done_button);
    lv_obj_set_style_text_font(button_label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(button_label, color_hex(0xFFFFFF), 0);
    lv_label_set_text(button_label, LV_SYMBOL_OK);
    lv_obj_center(button_label);

    s_keyboard = lv_keyboard_create(s_keyboard_page);
    lv_obj_set_pos(s_keyboard, 0, UI_KEYBOARD_HEADER_HEIGHT);
    lv_obj_set_size(s_keyboard, UI_HOR_RES, UI_KEYBOARD_HEIGHT);
    lv_obj_set_style_bg_color(s_keyboard, color_hex(0xCAD5DA),
                              LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_keyboard, color_hex(0xF8FAFB),
                              LV_PART_ITEMS);
    lv_obj_set_style_text_color(s_keyboard, color_hex(0x1D292F),
                                LV_PART_ITEMS);
    lv_obj_set_style_border_width(s_keyboard, 1, LV_PART_ITEMS);
    lv_obj_set_style_border_color(s_keyboard, color_hex(0x91A0A7),
                                  LV_PART_ITEMS);
    lv_obj_set_style_radius(s_keyboard, 4, LV_PART_ITEMS);
    lv_obj_add_event_cb(s_keyboard, keyboard_event_callback,
                        LV_EVENT_ALL, NULL);

    if (s_has_saved_wifi_credentials) {
        lv_textarea_set_text(s_password_textarea,
                             s_saved_wifi_credentials.password);
        lv_obj_add_flag(s_settings_page, LV_OBJ_FLAG_HIDDEN);
    } else {
        show_settings_page();
    }
    return ESP_OK;
}

static void clear_card(ui_card_t *card)
{
    if (NULL == card) {
        return;
    }
    lv_label_set_text(card->title, "");
    lv_label_set_text(card->client, "");
    lv_label_set_text(card->status, "");
    lv_label_set_text(card->elapsed, "");
    lv_obj_set_style_bg_color(card->background, color_hex(0x1B1F22), 0);
    lv_obj_set_style_border_width(card->background, 1, 0);
    lv_obj_set_style_border_color(card->background, color_hex(0x31373B), 0);
    lv_obj_set_style_outline_width(card->background, 0, 0);
}

static void render_model(const notifier_model_t *model,
                         const notifier_page_view_t *page)
{
    char text[32] = {0};
    bool online = false;

    if (NULL == model || NULL == page) {
        return;
    }
    online = notifier_model_is_online(model);

    snprintf(text, sizeof(text), "RUN %u", (unsigned)model->snapshot.running_count);
    lv_label_set_text(s_run_label, text);
    lv_obj_set_style_text_color(s_run_label, color_hex(0xF4C542), 0);
    snprintf(text, sizeof(text), "DONE %u", (unsigned)model->snapshot.done_count);
    lv_label_set_text(s_done_label, text);
    lv_obj_set_style_text_color(s_done_label, color_hex(0x38B56A), 0);
    lv_label_set_text(s_connection_label, online ? "ONLINE" : "OFFLINE");
    lv_obj_set_style_text_color(s_connection_label,
                                online ? color_hex(0x46B8C8)
                                       : color_hex(0xE45B5B),
                                0);

    for (uint8_t card_index = 0U; card_index < NOTIFIER_TASKS_PER_PAGE;
         ++card_index) {
        ui_card_t *card = &s_cards[card_index];
        uint8_t task_index = (uint8_t)(page->start_index + card_index);

        if (card_index >= page->row_count ||
            task_index >= model->snapshot.task_count) {
            clear_card(card);
            continue;
        }

        const notifier_task_t *task = &model->snapshot.tasks[task_index];
        bool highlighted = '\0' != page->highlight_task_id[0] &&
                           0 == strcmp(page->highlight_task_id, task->id);

        lv_label_set_text(card->title, task->title);
        lv_label_set_text(card->client, notifier_surface_name(task->surface));
        lv_label_set_text(card->status,
                          notifier_task_status_name(task->status));
        format_elapsed(task->elapsed_ms, text, sizeof(text));
        lv_label_set_text(card->elapsed, text);
        lv_obj_set_style_bg_color(
            card->background, card_background(task->status, online), 0);
        lv_obj_set_style_border_width(card->background, 1, 0);
        lv_obj_set_style_border_color(
            card->background, card_secondary_text(task->status, online), 0);
        lv_obj_set_style_outline_width(card->background,
                                       highlighted ? 2 : 0, 0);
        lv_obj_set_style_text_color(
            card->title, card_primary_text(task->status, online), 0);
        lv_obj_set_style_text_color(
            card->client, card_secondary_text(task->status, online), 0);
        lv_obj_set_style_text_color(
            card->status, card_secondary_text(task->status, online), 0);
        lv_obj_set_style_text_color(
            card->elapsed, card_primary_text(task->status, online), 0);
    }

    snprintf(text, sizeof(text), "PAGE %u/%u",
             (unsigned)page->page_index + 1U, (unsigned)page->page_count);
    lv_label_set_text(s_page_label, text);
    if (0U < model->snapshot.overflow_count) {
        snprintf(text, sizeof(text), "+%u",
                 (unsigned)model->snapshot.overflow_count);
        lv_label_set_text(s_overflow_label, text);
    } else {
        lv_label_set_text(s_overflow_label, "");
    }
}

static void render_alert_animation(const notifier_model_t *model,
                                   uint64_t now_ms)
{
    uint64_t elapsed_ms = 0U;
    uint32_t progress_width = 0U;
    bool active = false;

    if (NULL == model) {
        return;
    }
    active = 0U < model->alert_generation &&
             now_ms >= model->alert_animation_started_ms &&
             now_ms < model->alert_animation_until_ms;
    if (!active) {
        if (s_alert_visible) {
            lv_obj_add_flag(s_alert_overlay, LV_OBJ_FLAG_HIDDEN);
            s_alert_visible = false;
            ESP_LOGI(TAG, "[alert] animation done generation=%lu",
                     (unsigned long)s_rendered_alert_generation);
        }
        return;
    }

    if (s_rendered_alert_generation != model->alert_generation) {
        const char *title = ('\0' != model->alert_title[0])
                                ? model->alert_title
                                : "Task complete";

        s_rendered_alert_generation = model->alert_generation;
        lv_label_set_text(s_alert_title, title);
        lv_obj_clear_flag(s_alert_overlay, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(s_alert_overlay);
        s_alert_visible = true;
        ESP_LOGI(TAG,
                 "[alert] animation start generation=%lu duration_ms=%u "
                 "title_bytes=%u",
                 (unsigned long)s_rendered_alert_generation,
                 (unsigned)NOTIFIER_ALERT_ANIMATION_MS,
                 (unsigned)strlen(title));
    }

    elapsed_ms = now_ms - model->alert_animation_started_ms;
    if (elapsed_ms > NOTIFIER_ALERT_ANIMATION_MS) {
        elapsed_ms = NOTIFIER_ALERT_ANIMATION_MS;
    }
    progress_width = (uint32_t)((elapsed_ms * UI_ALERT_PROGRESS_WIDTH) /
                                NOTIFIER_ALERT_ANIMATION_MS);
    lv_obj_set_width(s_alert_progress, (lv_coord_t)progress_width);
    if (0U == ((elapsed_ms / UI_ALERT_FRAME_MS) % 2U)) {
        lv_obj_set_style_bg_color(s_alert_overlay, color_hex(0x123B2B), 0);
        lv_obj_set_y(s_alert_heading, 68);
    } else {
        lv_obj_set_style_bg_color(s_alert_overlay, color_hex(0x1D5A40), 0);
        lv_obj_set_y(s_alert_heading, 65);
    }
}

static esp_err_t initialize_lvgl(void)
{
    esp_err_t result = ESP_OK;
    size_t buffer_pixels = UI_HOR_RES * UI_BUFFER_ROWS;
    bool touch_ready = false;

    result = board_laiwfs300_display_init_with_config(UI_LCD_PCLK_HZ,
                                                      UI_BUFFER_ROWS);
    if (ESP_OK != result) {
        return result;
    }
    result = display_hal_set_orientation(true, false, true);
    if (ESP_OK != result) {
        return result;
    }
    for (uint32_t attempt = 1U; attempt <= UI_TOUCH_INIT_RETRY_COUNT;
         ++attempt) {
        result = board_laiwfs300_touch_init();
        if (ESP_OK == result) {
            touch_ready = true;
            break;
        }
        ESP_LOGW(TAG, "touch init attempt=%u/%u failed: %s",
                 (unsigned)attempt, (unsigned)UI_TOUCH_INIT_RETRY_COUNT,
                 esp_err_to_name(result));
        if (attempt < UI_TOUCH_INIT_RETRY_COUNT) {
            vTaskDelay(pdMS_TO_TICKS(UI_TOUCH_INIT_RETRY_MS));
        }
    }
    if (!touch_ready) {
        return result;
    }

    lv_init();
    result = notifier_font_init();
    if (ESP_OK != result) {
        return result;
    }
    s_draw_buffer_1 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t),
                                       MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    s_draw_buffer_2 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t),
                                       MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == s_draw_buffer_1 || NULL == s_draw_buffer_2) {
        heap_caps_free(s_draw_buffer_1);
        heap_caps_free(s_draw_buffer_2);
        return ESP_ERR_NO_MEM;
    }

    static lv_disp_draw_buf_t draw_buffer;
    static lv_disp_drv_t display_driver;
    static lv_indev_drv_t input_driver;
    lv_disp_draw_buf_init(&draw_buffer, s_draw_buffer_1, s_draw_buffer_2,
                          buffer_pixels);
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = UI_HOR_RES;
    display_driver.ver_res = UI_VER_RES;
    display_driver.flush_cb = flush_callback;
    display_driver.draw_buf = &draw_buffer;
    lv_disp_drv_register(&display_driver);

    lv_indev_drv_init(&input_driver);
    input_driver.type = LV_INDEV_TYPE_POINTER;
    input_driver.read_cb = touch_read_callback;
    lv_indev_drv_register(&input_driver);

    const esp_timer_create_args_t timer_arguments = {
        .callback = tick_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "notifier_lvgl",
        .skip_unhandled_events = true,
    };
    result = esp_timer_create(&timer_arguments, &s_tick_timer);
    if (ESP_OK != result) {
        return result;
    }
    result = esp_timer_start_periodic(s_tick_timer, UI_TICK_MS * 1000U);
    if (ESP_OK != result) {
        return result;
    }
    return create_screen();
}

static void ui_task(void *argument)
{
    ui_wifi_scan_update_t wifi_scan_update = {0};
    uint64_t last_model_refresh_ms = 0U;
    uint32_t last_revision = UINT32_MAX;
    uint8_t last_page = UINT8_MAX;
    uint64_t last_alert_frame_ms = 0U;
    char last_highlight[NOTIFIER_TASK_ID_MAX_BYTES + 1U] = {0};
    bool last_online = false;

    (void)argument;
    s_init_result = initialize_lvgl();
    xSemaphoreGive(s_init_semaphore);
    if (ESP_OK != s_init_result) {
        ESP_LOGE(TAG, "UI initialization failed: %s",
                 esp_err_to_name(s_init_result));
        vTaskDelete(NULL);
        return;
    }
    lv_timer_handler();
    ESP_LOGI(TAG,
             "UI initialized 320x240 cards=%u pclk=%u wait_per_flush=1",
             (unsigned)NOTIFIER_TASKS_PER_PAGE, (unsigned)UI_LCD_PCLK_HZ);

    while (true) {
        uint64_t now_ms = (uint64_t)(esp_timer_get_time() / 1000);
        if (NULL != s_wifi_scan_update_queue &&
            pdTRUE == xQueueReceive(s_wifi_scan_update_queue,
                                    &wifi_scan_update, 0)) {
            render_wifi_scan_update(&wifi_scan_update);
        }
        if (now_ms - last_model_refresh_ms >= UI_MODEL_REFRESH_MS &&
            NULL != s_copy_model &&
            s_copy_model(&s_ui_model, s_copy_model_context)) {
            notifier_page_view_t page = {0};
            bool online = notifier_model_is_online(&s_ui_model);

            notifier_model_get_page(&s_ui_model, now_ms, &page);
            if (s_ui_model.revision != last_revision ||
                page.page_index != last_page || online != last_online ||
                0 != strcmp(page.highlight_task_id, last_highlight)) {
                render_model(&s_ui_model, &page);
                last_revision = s_ui_model.revision;
                last_page = page.page_index;
                last_online = online;
                memcpy(last_highlight, page.highlight_task_id,
                       sizeof(last_highlight));
            }
            last_model_refresh_ms = now_ms;
        }
        if (now_ms - last_alert_frame_ms >= UI_ALERT_FRAME_MS) {
            render_alert_animation(&s_ui_model, now_ms);
            last_alert_frame_ms = now_ms;
        }
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(UI_TASK_DELAY_MS));
    }
}

esp_err_t notifier_ui_start(notifier_ui_copy_model_fn copy_model,
                            void *model_context,
                            const notifier_wifi_credentials_t *credentials,
                            bool has_credentials,
                            notifier_ui_save_wifi_fn save_wifi,
                            notifier_ui_request_wifi_scan_fn request_wifi_scan,
                            void *wifi_context)
{
    BaseType_t task_result = pdFAIL;

    if (NULL == copy_model || NULL == save_wifi ||
        NULL == request_wifi_scan ||
        (has_credentials &&
         (NULL == credentials ||
          NOTIFIER_WIFI_CONFIG_OK != notifier_wifi_config_validate(
              credentials->ssid, credentials->password)))) {
        return ESP_ERR_INVALID_ARG;
    }
    s_copy_model = copy_model;
    s_copy_model_context = model_context;
    s_save_wifi = save_wifi;
    s_request_wifi_scan = request_wifi_scan;
    s_save_wifi_context = wifi_context;
    memset(&s_saved_wifi_credentials, 0,
           sizeof(s_saved_wifi_credentials));
    s_has_saved_wifi_credentials = has_credentials;
    if (has_credentials) {
        s_saved_wifi_credentials = *credentials;
    }
    notifier_wifi_scan_list_init(&s_wifi_scan_list);
    s_wifi_scan_update_queue = xQueueCreate(1U,
                                            sizeof(ui_wifi_scan_update_t));
    if (NULL == s_wifi_scan_update_queue) {
        return ESP_ERR_NO_MEM;
    }
    s_init_semaphore = xSemaphoreCreateBinary();
    if (NULL == s_init_semaphore) {
        vQueueDelete(s_wifi_scan_update_queue);
        s_wifi_scan_update_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    task_result = xTaskCreatePinnedToCore(ui_task, "notifier_ui",
                                          UI_TASK_STACK, NULL,
                                          UI_TASK_PRIORITY, NULL,
                                          UI_TASK_CORE);
    if (pdPASS != task_result) {
        vSemaphoreDelete(s_init_semaphore);
        s_init_semaphore = NULL;
        vQueueDelete(s_wifi_scan_update_queue);
        s_wifi_scan_update_queue = NULL;
        return ESP_ERR_NO_MEM;
    }
    if (pdTRUE != xSemaphoreTake(s_init_semaphore,
                                 pdMS_TO_TICKS(UI_INIT_TIMEOUT_MS))) {
        return ESP_ERR_TIMEOUT;
    }
    vSemaphoreDelete(s_init_semaphore);
    s_init_semaphore = NULL;
    if (ESP_OK != s_init_result) {
        vQueueDelete(s_wifi_scan_update_queue);
        s_wifi_scan_update_queue = NULL;
    }
    return s_init_result;
}

esp_err_t notifier_ui_publish_wifi_scan(
    const notifier_wifi_scan_list_t *networks, esp_err_t scan_result)
{
    ui_wifi_scan_update_t update = {0};

    if (NULL == networks ||
        NOTIFIER_WIFI_SCAN_MAX_NETWORKS < networks->count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (NULL == s_wifi_scan_update_queue) {
        return ESP_ERR_INVALID_STATE;
    }
    update.networks = *networks;
    update.scan_result = scan_result;
    if (pdPASS != xQueueOverwrite(s_wifi_scan_update_queue, &update)) {
        return ESP_ERR_INVALID_STATE;
    }
    return ESP_OK;
}

uint32_t notifier_ui_flush_error_count(void)
{
    return s_flush_errors;
}
