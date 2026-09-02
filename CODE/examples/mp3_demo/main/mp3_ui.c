#include "mp3_ui.h"

#include "mp3_font.h"

#include "board_pins.h"
#include "display_hal.h"
#include "touch_hal.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "lvgl.h"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "mp3_ui";

#define MP3_UI_HOR_RES BOARD_LAIWFS300_LCD_V_RES
#define MP3_UI_VER_RES BOARD_LAIWFS300_LCD_H_RES
#define MP3_UI_TICK_MS 2U
#define MP3_UI_LCD_WAIT_MS 1000U
#define MP3_UI_TOUCH_MAX_POINTS 2U
#define MP3_UI_LCD_ERROR_LOG_INTERVAL 100U
#define MP3_UI_TRANSIENT_STATUS_MS 1500U
#define MP3_UI_TITLE_HEIGHT 26
#define MP3_UI_COVER_X 8
#define MP3_UI_COVER_Y 32
#define MP3_UI_LYRICS_X 112
#define MP3_UI_LYRICS_WIDTH 200
#define MP3_UI_PREVIOUS_LYRIC_Y 48
#define MP3_UI_CURRENT_LYRIC_Y 77
#define MP3_UI_NEXT_LYRIC_Y 108
#define MP3_UI_LYRIC_HEIGHT 24
#define MP3_UI_TIME_Y 137
#define MP3_UI_TIME_HEIGHT 16
#define MP3_UI_SLIDER_X 12
#define MP3_UI_SLIDER_Y 153
#define MP3_UI_SLIDER_WIDTH 296
#define MP3_UI_SLIDER_HEIGHT 20
#define MP3_UI_CONTROL_Y 180
#define MP3_UI_CONTROL_WIDTH 52
#define MP3_UI_CONTROL_HEIGHT 48
#define MP3_UI_CONTROL_FIRST_X 22
#define MP3_UI_CONTROL_GAP 17
#define MP3_UI_LIST_HEADER_HEIGHT 36
#define MP3_UI_LIST_ROW_HEIGHT 40
#define MP3_UI_LIST_ROW_GAP 2

typedef struct {
    mp3_ui_config_t config;
    esp_timer_handle_t tick_timer;
    lv_color_t *draw_buffer_1;
    lv_color_t *draw_buffer_2;
    lv_obj_t *play_page;
    lv_obj_t *list_page;
    lv_obj_t *title_label;
    lv_obj_t *cover_image;
    lv_obj_t *cover_placeholder;
    lv_obj_t *previous_lyric_label;
    lv_obj_t *current_lyric_label;
    lv_obj_t *next_lyric_label;
    lv_obj_t *status_label;
    lv_obj_t *current_time_label;
    lv_obj_t *duration_label;
    lv_obj_t *slider;
    lv_obj_t *toggle_label;
    lv_obj_t *list_container;
    lv_obj_t *list_count_label;
    lv_obj_t *song_buttons[MP3_CATALOG_MAX_SONGS];
    lv_img_dsc_t cover_descriptor;
    mp3_progress_t progress;
    mp3_player_snapshot_t latest_snapshot;
    const mp3_lrc_t *lyrics;
    const char *title;
    uint32_t generation;
    uint32_t lcd_errors;
    uint32_t touch_errors;
    uint16_t song_index;
    size_t lyric_index;
    int64_t transient_status_until_us;
    bool initialized;
    bool awaiting_seek;
    bool fatal_status;
} mp3_ui_context_t;

static mp3_ui_context_t s_ui;

static lv_color_t color_hex(uint32_t rgb)
{
    return lv_color_hex(rgb);
}

static void increment_saturated(uint32_t *value)
{
    if (NULL != value && UINT32_MAX > *value) {
        ++(*value);
    }
}

static void style_panel(lv_obj_t *object, lv_color_t background)
{
    if (NULL == object) {
        return;
    }
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(object, background, 0);
    lv_obj_clear_flag(object, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                              lv_coord_t width, lv_coord_t height,
                              const lv_font_t *font,
                              lv_text_align_t alignment,
                              lv_color_t color)
{
    if (NULL == parent || NULL == font) {
        return NULL;
    }
    lv_obj_t *label = lv_label_create(parent);
    if (NULL == label) {
        return NULL;
    }
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_align(label, alignment, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return label;
}

static void flush_callback(lv_disp_drv_t *driver, const lv_area_t *area,
                           lv_color_t *color_map)
{
    esp_err_t result = ESP_OK;

    if (NULL == driver || NULL == area || NULL == color_map ||
        NULL == s_ui.config.spi_lock) {
        if (NULL != driver) {
            lv_disp_flush_ready(driver);
        }
        return;
    }
    uint32_t pixel_count = (uint32_t)(area->x2 - area->x1 + 1) *
                           (uint32_t)(area->y2 - area->y1 + 1);
    uint16_t *pixels = (uint16_t *)color_map;
    for (uint32_t index = 0U; index < pixel_count; ++index) {
        uint16_t pixel = pixels[index];
        uint16_t red = (pixel >> 11U) & 0x1fU;
        uint16_t green = (pixel >> 5U) & 0x3fU;
        uint16_t blue = pixel & 0x1fU;
        uint16_t bgr = (uint16_t)((blue << 11U) | (green << 5U) | red);
        pixels[index] = (uint16_t)((bgr >> 8U) | (bgr << 8U));
    }

    if (!mp3_spi_lock_acquire(s_ui.config.spi_lock,
                              MP3_UI_LCD_WAIT_MS)) {
        result = ESP_ERR_TIMEOUT;
    } else {
        result = display_hal_draw_bitmap_rgb565(
            area->x1, area->y1, area->x2 - area->x1 + 1,
            area->y2 - area->y1 + 1, pixels);
        if (ESP_OK == result) {
            result = display_hal_wait_pending(MP3_UI_LCD_WAIT_MS);
        }
        mp3_spi_lock_release(s_ui.config.spi_lock);
    }
    if (ESP_OK != result) {
        increment_saturated(&s_ui.lcd_errors);
        if (1U == s_ui.lcd_errors ||
            0U == (s_ui.lcd_errors % MP3_UI_LCD_ERROR_LOG_INTERVAL)) {
            ESP_LOGE(TAG, "LCD error count=%" PRIu32 " result=%s",
                     s_ui.lcd_errors, esp_err_to_name(result));
        }
    }
    lv_disp_flush_ready(driver);
}

static lv_coord_t clamp_coordinate(int32_t value, int32_t maximum)
{
    if (0 > value) {
        return 0;
    }
    if (maximum < value) {
        return (lv_coord_t)maximum;
    }
    return (lv_coord_t)value;
}

static void touch_read_callback(lv_indev_drv_t *driver,
                                lv_indev_data_t *data)
{
    static lv_coord_t last_x;
    static lv_coord_t last_y;
    touch_panel_point_t point = {0};
    uint8_t touch_count = 0U;

    (void)driver;
    if (NULL == data) {
        return;
    }
    esp_err_t result = touch_panel_read_point(&point, &touch_count);
    if (ESP_OK == result && 0U < touch_count &&
        touch_count <= MP3_UI_TOUCH_MAX_POINTS) {
        int32_t mapped_x =
            (int32_t)MP3_UI_HOR_RES - 1 - (int32_t)point.y;
        int32_t mapped_y = (int32_t)point.x;
        last_x = clamp_coordinate(mapped_x, MP3_UI_HOR_RES - 1);
        last_y = clamp_coordinate(mapped_y, MP3_UI_VER_RES - 1);
        data->state = LV_INDEV_STATE_PR;
    } else {
        if (ESP_OK != result) {
            increment_saturated(&s_ui.touch_errors);
        }
        data->state = LV_INDEV_STATE_REL;
    }
    data->point.x = last_x;
    data->point.y = last_y;
}

static void tick_callback(void *context)
{
    (void)context;
    lv_tick_inc(MP3_UI_TICK_MS);
}

static void publish_command(const mp3_ui_command_t *command)
{
    if (NULL != command && NULL != s_ui.config.command_callback) {
        s_ui.config.command_callback(command, s_ui.config.command_context);
    }
}

static void simple_button_callback(lv_event_t *event)
{
    if (NULL == event || LV_EVENT_CLICKED != lv_event_get_code(event)) {
        return;
    }
    mp3_ui_command_t command = {
        .type = (mp3_ui_command_type_t)(uintptr_t)lv_event_get_user_data(event),
    };
    publish_command(&command);
}

static void show_play_page(void)
{
    if (NULL == s_ui.play_page || NULL == s_ui.list_page) {
        return;
    }
    lv_obj_add_flag(s_ui.list_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ui.play_page, LV_OBJ_FLAG_HIDDEN);
}

static void list_button_callback(lv_event_t *event)
{
    if (NULL == event || LV_EVENT_CLICKED != lv_event_get_code(event)) {
        return;
    }
    lv_obj_add_flag(s_ui.play_page, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ui.list_page, LV_OBJ_FLAG_HIDDEN);
}

static void back_button_callback(lv_event_t *event)
{
    if (NULL != event && LV_EVENT_CLICKED == lv_event_get_code(event)) {
        show_play_page();
    }
}

static void song_button_callback(lv_event_t *event)
{
    if (NULL == event || LV_EVENT_CLICKED != lv_event_get_code(event)) {
        return;
    }
    uintptr_t encoded = (uintptr_t)lv_event_get_user_data(event);
    if (0U == encoded || encoded > s_ui.config.song_count) {
        return;
    }
    mp3_ui_command_t command = {
        .type = MP3_UI_COMMAND_SELECT_SONG,
        .song_index = (uint16_t)(encoded - 1U),
    };
    show_play_page();
    publish_command(&command);
}

static void format_time(uint64_t time_ms, char *text, size_t text_size)
{
    if (NULL == text || 0U == text_size) {
        return;
    }
    uint64_t total_seconds = time_ms / 1000U;
    snprintf(text, text_size, "%" PRIu64 ":%02" PRIu64,
             total_seconds / 60U, total_seconds % 60U);
}

static void set_current_time(uint64_t time_ms)
{
    char text[24] = {0};
    format_time(time_ms, text, sizeof(text));
    lv_label_set_text(s_ui.current_time_label, text);
}

static void slider_callback(lv_event_t *event)
{
    if (NULL == event || NULL == s_ui.slider) {
        return;
    }
    lv_event_code_t code = lv_event_get_code(event);
    int32_t value = lv_slider_get_value(s_ui.slider);

    if (LV_EVENT_PRESSED == code) {
        (void)mp3_progress_begin_drag(&s_ui.progress);
        return;
    }
    if (LV_EVENT_VALUE_CHANGED == code &&
        mp3_progress_accept_value_event(&s_ui.progress)) {
        uint64_t preview_ms = 0U;
        if (mp3_progress_preview(&s_ui.progress, value, &preview_ms)) {
            set_current_time(preview_ms);
        }
        return;
    }
    if (LV_EVENT_RELEASED == code || LV_EVENT_PRESS_LOST == code) {
        mp3_ui_command_t command = {.type = MP3_UI_COMMAND_SEEK};
        if (mp3_progress_release(&s_ui.progress, value, &command.seek)) {
            s_ui.awaiting_seek = true;
            publish_command(&command);
        }
    }
}

static lv_obj_t *create_icon_button(lv_obj_t *parent, lv_coord_t x,
                                    const char *symbol,
                                    lv_event_cb_t event_callback,
                                    void *user_data)
{
    if (NULL == parent || NULL == symbol || NULL == event_callback) {
        return NULL;
    }
    lv_obj_t *button = lv_btn_create(parent);
    if (NULL == button) {
        return NULL;
    }
    lv_obj_set_pos(button, x, MP3_UI_CONTROL_Y);
    lv_obj_set_size(button, MP3_UI_CONTROL_WIDTH, MP3_UI_CONTROL_HEIGHT);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_border_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, color_hex(0x30363B), 0);
    lv_obj_set_style_bg_color(button, color_hex(0x3E765F), LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_add_event_cb(button, event_callback, LV_EVENT_CLICKED, user_data);

    lv_obj_t *label = lv_label_create(button);
    if (NULL == label) {
        return NULL;
    }
    lv_obj_set_style_text_font(label, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(label, color_hex(0xF2F5F3), 0);
    lv_label_set_text(label, symbol);
    lv_obj_center(label);
    return label;
}

static esp_err_t create_play_page(lv_obj_t *screen)
{
    s_ui.play_page = lv_obj_create(screen);
    if (NULL == s_ui.play_page) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_pos(s_ui.play_page, 0, 0);
    lv_obj_set_size(s_ui.play_page, MP3_UI_HOR_RES, MP3_UI_VER_RES);
    style_panel(s_ui.play_page, color_hex(0x121517));

    s_ui.title_label = create_label(
        s_ui.play_page, 8, 3, 304, MP3_UI_TITLE_HEIGHT,
        &g_mp3_font_noto_sans_sc_16, LV_TEXT_ALIGN_LEFT,
        color_hex(0xF1CB66));

    lv_obj_t *cover_panel = lv_obj_create(s_ui.play_page);
    if (NULL == cover_panel) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_pos(cover_panel, MP3_UI_COVER_X, MP3_UI_COVER_Y);
    lv_obj_set_size(cover_panel, MP3_UI_COVER_WIDTH, MP3_UI_COVER_HEIGHT);
    lv_obj_set_style_radius(cover_panel, 4, 0);
    lv_obj_set_style_border_width(cover_panel, 1, 0);
    lv_obj_set_style_border_color(cover_panel, color_hex(0x475057), 0);
    lv_obj_set_style_pad_all(cover_panel, 0, 0);
    lv_obj_set_style_bg_opa(cover_panel, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(cover_panel, color_hex(0x202528), 0);
    lv_obj_clear_flag(cover_panel, LV_OBJ_FLAG_SCROLLABLE);
    s_ui.cover_image = lv_img_create(cover_panel);
    lv_obj_set_pos(s_ui.cover_image, 0, 0);
    s_ui.cover_placeholder = create_label(
        cover_panel, 0, 31, MP3_UI_COVER_WIDTH, 34,
        &lv_font_montserrat_20, LV_TEXT_ALIGN_CENTER, color_hex(0x7A858B));
    lv_label_set_text(s_ui.cover_placeholder, LV_SYMBOL_AUDIO);
    lv_obj_add_flag(s_ui.cover_image, LV_OBJ_FLAG_HIDDEN);

    s_ui.previous_lyric_label = create_label(
        s_ui.play_page, MP3_UI_LYRICS_X, MP3_UI_PREVIOUS_LYRIC_Y,
        MP3_UI_LYRICS_WIDTH, MP3_UI_LYRIC_HEIGHT,
        &g_mp3_font_noto_sans_sc_16, LV_TEXT_ALIGN_CENTER,
        color_hex(0x778187));
    s_ui.current_lyric_label = create_label(
        s_ui.play_page, MP3_UI_LYRICS_X, MP3_UI_CURRENT_LYRIC_Y,
        MP3_UI_LYRICS_WIDTH, MP3_UI_LYRIC_HEIGHT,
        &g_mp3_font_noto_sans_sc_16, LV_TEXT_ALIGN_CENTER,
        color_hex(0xF4F6F5));
    s_ui.next_lyric_label = create_label(
        s_ui.play_page, MP3_UI_LYRICS_X, MP3_UI_NEXT_LYRIC_Y,
        MP3_UI_LYRICS_WIDTH, MP3_UI_LYRIC_HEIGHT,
        &g_mp3_font_noto_sans_sc_16, LV_TEXT_ALIGN_CENTER,
        color_hex(0x778187));
    s_ui.status_label = create_label(
        s_ui.play_page, MP3_UI_LYRICS_X, 70, MP3_UI_LYRICS_WIDTH, 48,
        &g_mp3_font_noto_sans_sc_16, LV_TEXT_ALIGN_CENTER,
        color_hex(0xD9DFDC));
    lv_label_set_long_mode(s_ui.status_label, LV_LABEL_LONG_WRAP);
    lv_obj_add_flag(s_ui.status_label, LV_OBJ_FLAG_HIDDEN);

    s_ui.current_time_label = create_label(
        s_ui.play_page, 12, MP3_UI_TIME_Y, 100, MP3_UI_TIME_HEIGHT,
        &lv_font_montserrat_12, LV_TEXT_ALIGN_LEFT, color_hex(0xC3CAC7));
    s_ui.duration_label = create_label(
        s_ui.play_page, 208, MP3_UI_TIME_Y, 100, MP3_UI_TIME_HEIGHT,
        &lv_font_montserrat_12, LV_TEXT_ALIGN_RIGHT, color_hex(0xC3CAC7));
    lv_label_set_text(s_ui.current_time_label, "0:00");
    lv_label_set_text(s_ui.duration_label, "0:00");

    s_ui.slider = lv_slider_create(s_ui.play_page);
    if (NULL == s_ui.slider) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_pos(s_ui.slider, MP3_UI_SLIDER_X, MP3_UI_SLIDER_Y);
    lv_obj_set_size(s_ui.slider, MP3_UI_SLIDER_WIDTH,
                    MP3_UI_SLIDER_HEIGHT);
    lv_slider_set_range(s_ui.slider, MP3_PROGRESS_SLIDER_MIN,
                        MP3_PROGRESS_SLIDER_MAX);
    lv_obj_set_style_bg_color(s_ui.slider, color_hex(0x3B4246), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_ui.slider, color_hex(0x38A87E),
                              LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_ui.slider, color_hex(0xF1CB66), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_ui.slider, 4, LV_PART_KNOB);
    lv_obj_add_event_cb(s_ui.slider, slider_callback, LV_EVENT_ALL, NULL);

    lv_coord_t x = MP3_UI_CONTROL_FIRST_X;
    if (NULL == create_icon_button(
            s_ui.play_page, x, LV_SYMBOL_PREV, simple_button_callback,
            (void *)(uintptr_t)MP3_UI_COMMAND_PREVIOUS)) {
        return ESP_ERR_NO_MEM;
    }
    x += MP3_UI_CONTROL_WIDTH + MP3_UI_CONTROL_GAP;
    s_ui.toggle_label = create_icon_button(
        s_ui.play_page, x, LV_SYMBOL_PLAY, simple_button_callback,
        (void *)(uintptr_t)MP3_UI_COMMAND_TOGGLE_PLAYBACK);
    if (NULL == s_ui.toggle_label) {
        return ESP_ERR_NO_MEM;
    }
    x += MP3_UI_CONTROL_WIDTH + MP3_UI_CONTROL_GAP;
    if (NULL == create_icon_button(
            s_ui.play_page, x, LV_SYMBOL_NEXT, simple_button_callback,
            (void *)(uintptr_t)MP3_UI_COMMAND_NEXT)) {
        return ESP_ERR_NO_MEM;
    }
    x += MP3_UI_CONTROL_WIDTH + MP3_UI_CONTROL_GAP;
    if (NULL == create_icon_button(s_ui.play_page, x, LV_SYMBOL_LIST,
                                   list_button_callback, NULL)) {
        return ESP_ERR_NO_MEM;
    }
    return (NULL != s_ui.title_label && NULL != s_ui.previous_lyric_label &&
            NULL != s_ui.current_lyric_label &&
            NULL != s_ui.next_lyric_label && NULL != s_ui.status_label)
               ? ESP_OK
               : ESP_ERR_NO_MEM;
}

static esp_err_t populate_song_list(void)
{
    if (NULL == s_ui.list_container || NULL == s_ui.list_count_label ||
        s_ui.config.song_count > MP3_CATALOG_MAX_SONGS ||
        (0U < s_ui.config.song_count && NULL == s_ui.config.songs)) {
        return ESP_ERR_INVALID_STATE;
    }
    char count_text[16] = {0};
    snprintf(count_text, sizeof(count_text), "%u",
             (unsigned int)s_ui.config.song_count);
    lv_label_set_text(s_ui.list_count_label, count_text);

    if (0U == s_ui.config.song_count) {
        lv_obj_t *empty = create_label(
            s_ui.list_container, 0, 60, 304, 30,
            &g_mp3_font_noto_sans_sc_16, LV_TEXT_ALIGN_CENTER,
            color_hex(0x8B959A));
        if (NULL == empty) {
            return ESP_ERR_NO_MEM;
        }
        lv_label_set_text(empty, "暂无歌曲");
        return ESP_OK;
    }
    for (size_t index = 0U; index < s_ui.config.song_count; ++index) {
        lv_obj_t *button = lv_btn_create(s_ui.list_container);
        if (NULL == button) {
            return ESP_ERR_NO_MEM;
        }
        s_ui.song_buttons[index] = button;
        lv_obj_set_width(button, LV_PCT(100));
        lv_obj_set_height(button, MP3_UI_LIST_ROW_HEIGHT);
        lv_obj_set_flex_grow(button, 0);
        lv_obj_set_style_radius(button, 3, 0);
        lv_obj_set_style_border_width(button, 0, 0);
        lv_obj_set_style_shadow_width(button, 0, 0);
        lv_obj_set_style_bg_color(button,
                                  color_hex((0U == (index % 2U))
                                                ? 0x252A2D
                                                : 0x202427),
                                  0);
        lv_obj_set_style_bg_color(button, color_hex(0x655B34),
                                  LV_STATE_CHECKED);
        lv_obj_set_style_bg_color(button, color_hex(0x3C6455),
                                  LV_STATE_PRESSED);
        lv_obj_add_flag(button, LV_OBJ_FLAG_CHECKABLE);
        lv_obj_add_event_cb(button, song_button_callback, LV_EVENT_CLICKED,
                            (void *)(uintptr_t)(index + 1U));

        lv_obj_t *label = create_label(
            button, 8, 8, 292, 22, &g_mp3_font_noto_sans_sc_16,
            LV_TEXT_ALIGN_LEFT, color_hex(0xE9EDEB));
        if (NULL == label) {
            return ESP_ERR_NO_MEM;
        }
        lv_label_set_text(label, s_ui.config.songs[index].title);
    }
    return ESP_OK;
}

static esp_err_t create_list_page(lv_obj_t *screen)
{
    s_ui.list_page = lv_obj_create(screen);
    if (NULL == s_ui.list_page) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_pos(s_ui.list_page, 0, 0);
    lv_obj_set_size(s_ui.list_page, MP3_UI_HOR_RES, MP3_UI_VER_RES);
    style_panel(s_ui.list_page, color_hex(0x141719));

    lv_obj_t *header = lv_obj_create(s_ui.list_page);
    if (NULL == header) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, MP3_UI_HOR_RES, MP3_UI_LIST_HEADER_HEIGHT);
    style_panel(header, color_hex(0x235C47));

    lv_obj_t *back_button = lv_btn_create(header);
    if (NULL == back_button) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_pos(back_button, 2, 2);
    lv_obj_set_size(back_button, 42, 32);
    lv_obj_set_style_radius(back_button, 3, 0);
    lv_obj_set_style_border_width(back_button, 0, 0);
    lv_obj_set_style_bg_opa(back_button, LV_OPA_TRANSP, 0);
    lv_obj_add_event_cb(back_button, back_button_callback, LV_EVENT_CLICKED,
                        NULL);
    lv_obj_t *back_icon = lv_label_create(back_button);
    lv_obj_set_style_text_font(back_icon, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(back_icon, color_hex(0xF5F7F6), 0);
    lv_label_set_text(back_icon, LV_SYMBOL_LEFT);
    lv_obj_center(back_icon);

    lv_obj_t *heading = create_label(
        header, 48, 7, 190, 24, &g_mp3_font_noto_sans_sc_16,
        LV_TEXT_ALIGN_LEFT, color_hex(0xF5F7F6));
    s_ui.list_count_label = create_label(
        header, 238, 9, 72, 20, &lv_font_montserrat_12,
        LV_TEXT_ALIGN_RIGHT, color_hex(0xCFDDD7));
    if (NULL == heading || NULL == s_ui.list_count_label) {
        return ESP_ERR_NO_MEM;
    }
    lv_label_set_text(heading, "歌曲");

    s_ui.list_container = lv_obj_create(s_ui.list_page);
    if (NULL == s_ui.list_container) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_pos(s_ui.list_container, 0, MP3_UI_LIST_HEADER_HEIGHT);
    lv_obj_set_size(s_ui.list_container, MP3_UI_HOR_RES,
                    MP3_UI_VER_RES - MP3_UI_LIST_HEADER_HEIGHT);
    lv_obj_set_style_radius(s_ui.list_container, 0, 0);
    lv_obj_set_style_border_width(s_ui.list_container, 0, 0);
    lv_obj_set_style_pad_left(s_ui.list_container, 4, 0);
    lv_obj_set_style_pad_right(s_ui.list_container, 4, 0);
    lv_obj_set_style_pad_top(s_ui.list_container, 4, 0);
    lv_obj_set_style_pad_bottom(s_ui.list_container, 4, 0);
    lv_obj_set_style_pad_row(s_ui.list_container, MP3_UI_LIST_ROW_GAP, 0);
    lv_obj_set_style_bg_color(s_ui.list_container, color_hex(0x141719), 0);
    lv_obj_set_style_bg_opa(s_ui.list_container, LV_OPA_COVER, 0);
    lv_obj_set_flex_flow(s_ui.list_container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_ui.list_container, LV_DIR_VER);
    lv_obj_set_scrollbar_mode(s_ui.list_container, LV_SCROLLBAR_MODE_AUTO);

    esp_err_t result = populate_song_list();
    if (ESP_OK != result) {
        return result;
    }
    lv_obj_add_flag(s_ui.list_page, LV_OBJ_FLAG_HIDDEN);
    return ESP_OK;
}

static esp_err_t create_ui(void)
{
    lv_obj_t *screen = lv_scr_act();
    if (NULL == screen) {
        return ESP_ERR_NO_MEM;
    }
    style_panel(screen, color_hex(0x121517));
    esp_err_t result = create_play_page(screen);
    if (ESP_OK == result) {
        result = create_list_page(screen);
    }
    if (ESP_OK == result) {
        mp3_ui_show_status("MP3", "正在加载...", false);
    }
    return result;
}

esp_err_t mp3_ui_init(const mp3_ui_config_t *config)
{
    if (NULL == config || NULL == config->spi_lock ||
        config->song_count > MP3_CATALOG_MAX_SONGS ||
        (0U < config->song_count && NULL == config->songs)) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(&s_ui, 0, sizeof(s_ui));
    s_ui.config = *config;
    s_ui.lyric_index = MP3_LRC_INDEX_NONE;
    mp3_progress_init(&s_ui.progress);

    lv_init();
    esp_err_t result = mp3_font_init();
    if (ESP_OK != result) {
        return result;
    }
    size_t buffer_pixels =
        (size_t)MP3_UI_HOR_RES * MP3_UI_LCD_BUFFER_LINES;
    s_ui.draw_buffer_1 = heap_caps_malloc(
        buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    s_ui.draw_buffer_2 = heap_caps_malloc(
        buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == s_ui.draw_buffer_1 || NULL == s_ui.draw_buffer_2) {
        heap_caps_free(s_ui.draw_buffer_1);
        heap_caps_free(s_ui.draw_buffer_2);
        return ESP_ERR_NO_MEM;
    }

    static lv_disp_draw_buf_t draw_buffer;
    static lv_disp_drv_t display_driver;
    lv_disp_draw_buf_init(&draw_buffer, s_ui.draw_buffer_1,
                          s_ui.draw_buffer_2, buffer_pixels);
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = MP3_UI_HOR_RES;
    display_driver.ver_res = MP3_UI_VER_RES;
    display_driver.flush_cb = flush_callback;
    display_driver.draw_buf = &draw_buffer;
    if (NULL == lv_disp_drv_register(&display_driver)) {
        return ESP_FAIL;
    }

    if (config->touch_available) {
        static lv_indev_drv_t input_driver;
        lv_indev_drv_init(&input_driver);
        input_driver.type = LV_INDEV_TYPE_POINTER;
        input_driver.read_cb = touch_read_callback;
        if (NULL == lv_indev_drv_register(&input_driver)) {
            return ESP_FAIL;
        }
    }

    const esp_timer_create_args_t timer_arguments = {
        .callback = tick_callback,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "mp3_lvgl_tick",
        .skip_unhandled_events = true,
    };
    result = esp_timer_create(&timer_arguments, &s_ui.tick_timer);
    if (ESP_OK == result) {
        result = esp_timer_start_periodic(s_ui.tick_timer,
                                          MP3_UI_TICK_MS * 1000U);
    }
    if (ESP_OK == result) {
        result = create_ui();
    }
    s_ui.initialized = (ESP_OK == result);
    return result;
}

esp_err_t mp3_ui_set_catalog(const mp3_song_t *songs, size_t song_count)
{
    if (!s_ui.initialized || song_count > MP3_CATALOG_MAX_SONGS ||
        (0U < song_count && NULL == songs) || NULL == s_ui.list_container) {
        return ESP_ERR_INVALID_ARG;
    }
    lv_obj_clean(s_ui.list_container);
    memset(s_ui.song_buttons, 0, sizeof(s_ui.song_buttons));
    s_ui.config.songs = songs;
    s_ui.config.song_count = song_count;
    return populate_song_list();
}

void mp3_ui_process(void)
{
    if (!s_ui.initialized) {
        return;
    }
    if (!s_ui.fatal_status && 0 < s_ui.transient_status_until_us &&
        esp_timer_get_time() >= s_ui.transient_status_until_us) {
        s_ui.transient_status_until_us = 0;
        lv_obj_add_flag(s_ui.status_label, LV_OBJ_FLAG_HIDDEN);
    }
    (void)lv_timer_handler();
}

void mp3_ui_show_status(const char *title, const char *message,
                        bool is_error)
{
    if (NULL == title || NULL == message || NULL == s_ui.title_label ||
        NULL == s_ui.status_label) {
        return;
    }
    lv_label_set_text(s_ui.title_label, title);
    lv_label_set_text(s_ui.status_label, message);
    lv_obj_set_style_text_color(s_ui.status_label,
                                color_hex(is_error ? 0xF18B82 : 0xD9DFDC), 0);
    lv_obj_clear_flag(s_ui.status_label, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(s_ui.previous_lyric_label, "");
    lv_label_set_text(s_ui.current_lyric_label, "");
    lv_label_set_text(s_ui.next_lyric_label, "");
    s_ui.fatal_status = is_error;
}

static void select_list_row(uint16_t song_index)
{
    for (size_t index = 0U; index < s_ui.config.song_count; ++index) {
        if (NULL == s_ui.song_buttons[index]) {
            continue;
        }
        if (index == song_index) {
            lv_obj_add_state(s_ui.song_buttons[index], LV_STATE_CHECKED);
        } else {
            lv_obj_clear_state(s_ui.song_buttons[index], LV_STATE_CHECKED);
        }
    }
}

static void clear_cover(void)
{
    if (NULL == s_ui.cover_image || NULL == s_ui.cover_placeholder) {
        return;
    }
    lv_obj_add_flag(s_ui.cover_image, LV_OBJ_FLAG_HIDDEN);
    lv_obj_clear_flag(s_ui.cover_placeholder, LV_OBJ_FLAG_HIDDEN);
    memset(&s_ui.cover_descriptor, 0, sizeof(s_ui.cover_descriptor));
}

void mp3_ui_clear_song(uint32_t generation, uint16_t song_index,
                       const char *title)
{
    if (!s_ui.initialized || NULL == title) {
        return;
    }
    s_ui.generation = generation;
    s_ui.song_index = song_index;
    s_ui.title = title;
    s_ui.lyrics = NULL;
    s_ui.lyric_index = MP3_LRC_INDEX_NONE;
    s_ui.awaiting_seek = false;
    s_ui.fatal_status = false;
    s_ui.transient_status_until_us = 0;
    mp3_progress_init(&s_ui.progress);
    mp3_progress_set_snapshot(&s_ui.progress, generation, 0U, 0U, false);
    memset(&s_ui.latest_snapshot, 0, sizeof(s_ui.latest_snapshot));
    s_ui.latest_snapshot.generation = generation;
    s_ui.latest_snapshot.song_index = song_index;
    s_ui.latest_snapshot.state = MP3_PLAYER_STATE_LOADING;

    lv_label_set_text(s_ui.title_label, title);
    lv_label_set_text(s_ui.previous_lyric_label, "");
    lv_label_set_text(s_ui.current_lyric_label, "正在加载...");
    lv_label_set_text(s_ui.next_lyric_label, "");
    lv_obj_add_flag(s_ui.status_label, LV_OBJ_FLAG_HIDDEN);
    clear_cover();
    select_list_row(song_index);
    set_current_time(0U);
    lv_label_set_text(s_ui.duration_label, "0:00");
    mp3_progress_set_programmatic_update(&s_ui.progress, true);
    lv_slider_set_value(s_ui.slider, MP3_PROGRESS_SLIDER_MIN, LV_ANIM_OFF);
    mp3_progress_set_programmatic_update(&s_ui.progress, false);
    lv_obj_add_state(s_ui.slider, LV_STATE_DISABLED);
    lv_label_set_text(s_ui.toggle_label, LV_SYMBOL_PAUSE);
}

static void update_lyrics(uint64_t position_ms)
{
    if (NULL == s_ui.title || NULL == s_ui.previous_lyric_label ||
        NULL == s_ui.current_lyric_label || NULL == s_ui.next_lyric_label) {
        return;
    }
    if (NULL == s_ui.lyrics || 0U == s_ui.lyrics->line_count) {
        if (MP3_LRC_INDEX_NONE != s_ui.lyric_index) {
            s_ui.lyric_index = MP3_LRC_INDEX_NONE;
        }
        lv_label_set_text(s_ui.previous_lyric_label, "");
        lv_label_set_text(s_ui.current_lyric_label, "暂无歌词");
        lv_label_set_text(s_ui.next_lyric_label, "");
        return;
    }

    size_t current = mp3_lrc_find_line(s_ui.lyrics, position_ms);
    if (current == s_ui.lyric_index) {
        return;
    }
    s_ui.lyric_index = current;
    if (MP3_LRC_INDEX_NONE == current) {
        lv_label_set_text(s_ui.previous_lyric_label, "");
        lv_label_set_text(s_ui.current_lyric_label, s_ui.title);
        lv_label_set_text(s_ui.next_lyric_label,
                          mp3_lrc_get_text(s_ui.lyrics, 0U));
        return;
    }
    lv_label_set_text(s_ui.previous_lyric_label,
                      (0U < current)
                          ? mp3_lrc_get_text(s_ui.lyrics, current - 1U)
                          : "");
    lv_label_set_text(s_ui.current_lyric_label,
                      mp3_lrc_get_text(s_ui.lyrics, current));
    lv_label_set_text(s_ui.next_lyric_label,
                      (current + 1U < s_ui.lyrics->line_count)
                          ? mp3_lrc_get_text(s_ui.lyrics, current + 1U)
                          : "");
}

void mp3_ui_show_song(uint32_t generation, uint16_t song_index,
                      const char *title, const mp3_lrc_t *lyrics,
                      const mp3_cover_t *cover)
{
    if (!s_ui.initialized || generation != s_ui.generation ||
        song_index != s_ui.song_index || NULL == title) {
        return;
    }
    s_ui.title = title;
    s_ui.lyrics = lyrics;
    s_ui.lyric_index = MP3_LRC_INDEX_NONE;
    lv_label_set_text(s_ui.title_label, title);
    lv_obj_add_flag(s_ui.status_label, LV_OBJ_FLAG_HIDDEN);
    if (NULL != cover && NULL != cover->pixels &&
        MP3_UI_COVER_WIDTH == cover->width &&
        MP3_UI_COVER_HEIGHT == cover->height) {
        memset(&s_ui.cover_descriptor, 0, sizeof(s_ui.cover_descriptor));
        s_ui.cover_descriptor.header.always_zero = 0U;
        s_ui.cover_descriptor.header.w = cover->width;
        s_ui.cover_descriptor.header.h = cover->height;
        s_ui.cover_descriptor.header.cf = LV_IMG_CF_TRUE_COLOR;
        s_ui.cover_descriptor.data_size =
            (uint32_t)((size_t)cover->width * cover->height *
                       sizeof(uint16_t));
        s_ui.cover_descriptor.data = (const uint8_t *)cover->pixels;
        lv_img_set_src(s_ui.cover_image, &s_ui.cover_descriptor);
        lv_obj_clear_flag(s_ui.cover_image, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_ui.cover_placeholder, LV_OBJ_FLAG_HIDDEN);
    } else {
        clear_cover();
    }
    update_lyrics(0U);
}

void mp3_ui_update_snapshot(const mp3_player_snapshot_t *snapshot)
{
    if (!s_ui.initialized || NULL == snapshot ||
        snapshot->generation != s_ui.generation ||
        snapshot->song_index != s_ui.song_index) {
        return;
    }
    bool paused = MP3_PLAYER_STATE_PAUSED == snapshot->state ||
                  (MP3_PLAYER_STATE_SEEKING == snapshot->state &&
                   s_ui.progress.paused);
    s_ui.latest_snapshot = *snapshot;
    mp3_progress_set_snapshot(&s_ui.progress, snapshot->generation,
                              snapshot->position_ms, snapshot->duration_ms,
                              paused);
    if (0U == snapshot->duration_ms) {
        lv_obj_add_state(s_ui.slider, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(s_ui.slider, LV_STATE_DISABLED);
    }
    if (!s_ui.progress.dragging && !s_ui.awaiting_seek) {
        int32_t slider_value = mp3_progress_ms_to_value(
            snapshot->position_ms, snapshot->duration_ms);
        mp3_progress_set_programmatic_update(&s_ui.progress, true);
        lv_slider_set_value(s_ui.slider, slider_value, LV_ANIM_OFF);
        mp3_progress_set_programmatic_update(&s_ui.progress, false);
        set_current_time(snapshot->position_ms);
    }
    char duration_text[24] = {0};
    format_time(snapshot->duration_ms, duration_text, sizeof(duration_text));
    lv_label_set_text(s_ui.duration_label, duration_text);
    update_lyrics(snapshot->position_ms);

    if (MP3_PLAYER_STATE_PAUSED == snapshot->state ||
        MP3_PLAYER_STATE_STOPPED == snapshot->state ||
        MP3_PLAYER_STATE_ERROR == snapshot->state) {
        lv_label_set_text(s_ui.toggle_label, LV_SYMBOL_PLAY);
    } else {
        lv_label_set_text(s_ui.toggle_label, LV_SYMBOL_PAUSE);
    }
}

void mp3_ui_seek_completed(uint32_t generation, esp_err_t result)
{
    if (!s_ui.initialized || generation != s_ui.generation) {
        return;
    }
    s_ui.awaiting_seek = false;
    if (ESP_OK != result) {
        lv_label_set_text(s_ui.status_label, "跳转失败");
        lv_obj_set_style_text_color(s_ui.status_label, color_hex(0xF18B82), 0);
        lv_obj_clear_flag(s_ui.status_label, LV_OBJ_FLAG_HIDDEN);
        s_ui.fatal_status = false;
        s_ui.transient_status_until_us =
            esp_timer_get_time() +
            ((int64_t)MP3_UI_TRANSIENT_STATUS_MS * 1000);
    }
}

uint32_t mp3_ui_get_lcd_error_count(void)
{
    return s_ui.lcd_errors;
}

uint32_t mp3_ui_get_touch_error_count(void)
{
    return s_ui.touch_errors;
}
