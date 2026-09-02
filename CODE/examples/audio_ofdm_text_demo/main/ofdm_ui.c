#include "ofdm_ui.h"

#include "ofdm_font.h"

#include "board_pins.h"
#include "display_hal.h"
#include "touch_hal.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "lvgl.h"

#include <stdio.h>
#include <string.h>

#define OFDM_UI_HOR_RES BOARD_LAIWFS300_LCD_V_RES
#define OFDM_UI_VER_RES BOARD_LAIWFS300_LCD_H_RES
#define OFDM_UI_TICK_MS 2U
#define OFDM_UI_LCD_WAIT_MS 1000U
#define OFDM_UI_TOUCH_MAX_POINTS 2U
#define OFDM_UI_HEADER_HEIGHT 38
#define OFDM_UI_TEXT_X 10
#define OFDM_UI_TEXT_Y 48
#define OFDM_UI_TEXT_WIDTH 300
#define OFDM_UI_TEXT_HEIGHT 118
#define OFDM_UI_PROGRESS_Y 172
#define OFDM_UI_PROGRESS_HEIGHT 16
#define OFDM_UI_SEND_X 214
#define OFDM_UI_SEND_Y 194
#define OFDM_UI_SEND_WIDTH 96
#define OFDM_UI_SEND_HEIGHT 38
#define OFDM_UI_BUTTON_GAP 8
#define OFDM_UI_CLEAR_WIDTH 96
#define OFDM_UI_CLEAR_HEIGHT 38
#define OFDM_UI_CLEAR_X \
    (OFDM_UI_SEND_X - OFDM_UI_BUTTON_GAP - OFDM_UI_CLEAR_WIDTH)
#define OFDM_UI_CLEAR_Y OFDM_UI_SEND_Y
#define OFDM_UI_MESSAGE_BG_DEFAULT 0x171D20U
#define OFDM_UI_MESSAGE_BORDER_DEFAULT 0x334047U
#define OFDM_UI_MESSAGE_TEXT_DEFAULT 0xEDF1EFU
#define OFDM_UI_MESSAGE_BG_SUCCESS 0x132B22U
#define OFDM_UI_MESSAGE_BORDER_SUCCESS 0x4DBA91U
#define OFDM_UI_MESSAGE_TEXT_SUCCESS 0xEAFBF4U

typedef struct {
    esp_timer_handle_t tick_timer;
    lv_color_t *draw_buffer_1;
    lv_color_t *draw_buffer_2;
    lv_obj_t *state_label;
    lv_obj_t *detail_label;
    lv_obj_t *message_area;
    lv_obj_t *message_label;
    lv_obj_t *progress_bar;
    lv_obj_t *progress_label;
    lv_obj_t *clear_button;
    lv_obj_t *clear_icon;
    lv_obj_t *clear_label;
    lv_obj_t *send_button;
    lv_obj_t *send_icon;
    lv_obj_t *send_label;
    ofdm_link_state_t last_state;
    uint16_t last_received_session_id;
    uint32_t lcd_errors;
    uint32_t touch_errors;
    bool message_cleared;
    bool message_visible;
    bool initialized;
} ofdm_ui_context_t;

static const char *TAG = "ofdm_ui";
static ofdm_ui_context_t s_ui;

static void increment_saturated(uint32_t *value)
{
    if (NULL != value && UINT32_MAX > *value) {
        ++(*value);
    }
}

static lv_color_t color_hex(uint32_t rgb)
{
    return lv_color_hex(rgb);
}

static void style_surface(lv_obj_t *object, lv_color_t color)
{
    lv_obj_set_style_radius(object, 0, 0);
    lv_obj_set_style_border_width(object, 0, 0);
    lv_obj_set_style_pad_all(object, 0, 0);
    lv_obj_set_style_bg_opa(object, LV_OPA_COVER, 0);
    lv_obj_set_style_bg_color(object, color, 0);
}

static void flush_callback(lv_disp_drv_t *driver, const lv_area_t *area,
                           lv_color_t *color_map)
{
    if (NULL == driver || NULL == area || NULL == color_map) {
        if (NULL != driver) {
            lv_disp_flush_ready(driver);
        }
        return;
    }
    const uint32_t pixel_count =
        (uint32_t)(area->x2 - area->x1 + 1) *
        (uint32_t)(area->y2 - area->y1 + 1);
    uint16_t *pixels = (uint16_t *)color_map;
    for (uint32_t index = 0U; index < pixel_count; ++index) {
        const uint16_t pixel = pixels[index];
        const uint16_t red = (pixel >> 11U) & 0x1fU;
        const uint16_t green = (pixel >> 5U) & 0x3fU;
        const uint16_t blue = pixel & 0x1fU;
        const uint16_t bgr = (uint16_t)((blue << 11U) |
                                        (green << 5U) | red);
        pixels[index] = (uint16_t)((bgr >> 8U) | (bgr << 8U));
    }
    esp_err_t result = display_hal_draw_bitmap_rgb565(
        area->x1, area->y1, area->x2 - area->x1 + 1,
        area->y2 - area->y1 + 1, pixels);
    if (ESP_OK == result) {
        result = display_hal_wait_pending(OFDM_UI_LCD_WAIT_MS);
    }
    if (ESP_OK != result) {
        increment_saturated(&s_ui.lcd_errors);
        if (1U == s_ui.lcd_errors || 0U == (s_ui.lcd_errors % 100U)) {
            ESP_LOGE(TAG, "LCD error count=%lu result=%s",
                     (unsigned long)s_ui.lcd_errors,
                     esp_err_to_name(result));
        }
    }
    lv_disp_flush_ready(driver);
}

static lv_coord_t clamp_coordinate(int32_t value, int32_t maximum)
{
    if (0 > value) {
        return 0;
    }
    return maximum < value ? (lv_coord_t)maximum : (lv_coord_t)value;
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
        touch_count <= OFDM_UI_TOUCH_MAX_POINTS) {
        last_x = clamp_coordinate(
            (int32_t)OFDM_UI_HOR_RES - 1 - (int32_t)point.y,
            OFDM_UI_HOR_RES - 1);
        last_y = clamp_coordinate((int32_t)point.x,
                                  OFDM_UI_VER_RES - 1);
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
    lv_tick_inc(OFDM_UI_TICK_MS);
}

static void send_callback(lv_event_t *event)
{
    if (NULL == event || LV_EVENT_CLICKED != lv_event_get_code(event)) {
        return;
    }
    const bool queued = ofdm_link_request_send();
    ESP_LOGI(TAG, "OFDM_TX action=touch result=%s",
             queued ? "QUEUED" : "REJECTED");
    if (!queued) {
        lv_label_set_text(s_ui.detail_label, "当前链路忙，请稍候");
    }
}

static lv_obj_t *create_label(lv_obj_t *parent, lv_coord_t x, lv_coord_t y,
                              lv_coord_t width, lv_coord_t height,
                              const lv_font_t *font, lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    if (NULL == label) {
        return NULL;
    }
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_obj_set_style_pad_all(label, 0, 0);
    return label;
}

static const char *message_placeholder(ofdm_link_state_t state)
{
    switch (state) {
        case OFDM_LINK_STATE_BOOT:
            return "正在初始化声学链路...";
        case OFDM_LINK_STATE_RX_SYNC:
            return "检测到声波，正在同步...";
        case OFDM_LINK_STATE_RX_DATA:
            return "正在接收并校验数据...";
        case OFDM_LINK_STATE_RX_ERROR:
            return "本次接收未完成";
        case OFDM_LINK_STATE_TX_DATA:
            return "正在发送内置测试文字...";
        case OFDM_LINK_STATE_TX_DONE:
            return "声波已发出，请查看接收端";
        case OFDM_LINK_STATE_ERROR:
            return "声学链路发生错误";
        case OFDM_LINK_STATE_IDLE_RX:
        case OFDM_LINK_STATE_RX_OK:
        default:
            return "等待接收文字...";
    }
}

static void style_message_result(bool received_message)
{
    const uint32_t background = received_message
                                    ? OFDM_UI_MESSAGE_BG_SUCCESS
                                    : OFDM_UI_MESSAGE_BG_DEFAULT;
    const uint32_t border = received_message
                                ? OFDM_UI_MESSAGE_BORDER_SUCCESS
                                : OFDM_UI_MESSAGE_BORDER_DEFAULT;
    const uint32_t text = received_message
                              ? OFDM_UI_MESSAGE_TEXT_SUCCESS
                              : OFDM_UI_MESSAGE_TEXT_DEFAULT;
    lv_obj_set_style_bg_color(s_ui.message_area, color_hex(background), 0);
    lv_obj_set_style_border_color(s_ui.message_area, color_hex(border), 0);
    lv_obj_set_style_text_color(s_ui.message_label, color_hex(text), 0);
}

static void clear_callback(lv_event_t *event)
{
    if (NULL == event || LV_EVENT_CLICKED != lv_event_get_code(event)) {
        return;
    }
    if (!s_ui.message_visible) {
        ESP_LOGI(TAG, "OFDM_UI action=clear result=IGNORED");
        return;
    }

    s_ui.message_cleared = true;
    s_ui.message_visible = false;
    lv_label_set_text(s_ui.message_label, "等待接收文字...");
    lv_obj_scroll_to_y(s_ui.message_area, 0, LV_ANIM_OFF);
    style_message_result(false);
    lv_bar_set_value(s_ui.progress_bar, 0, LV_ANIM_OFF);
    lv_label_set_text(s_ui.progress_label, "0 bytes");
    lv_obj_add_state(s_ui.clear_button, LV_STATE_DISABLED);
    ESP_LOGI(TAG, "OFDM_UI action=clear result=CLEARED");
}

static esp_err_t create_ui(void)
{
    lv_obj_t *screen = lv_scr_act();
    if (NULL == screen) {
        return ESP_ERR_NO_MEM;
    }
    style_surface(screen, color_hex(0x101416));

    lv_obj_t *header = lv_obj_create(screen);
    lv_obj_set_pos(header, 0, 0);
    lv_obj_set_size(header, OFDM_UI_HOR_RES, OFDM_UI_HEADER_HEIGHT);
    style_surface(header, color_hex(0x1D2529));
    s_ui.state_label = create_label(header, 10, 5, 120, 25,
                                    &lv_font_montserrat_20,
                                    color_hex(0x77D6B7));
    s_ui.detail_label = create_label(header, 132, 9, 178, 20,
                                     &g_ofdm_font_noto_sans_sc_16,
                                     color_hex(0xD2DADC));
    if (NULL == s_ui.state_label || NULL == s_ui.detail_label) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_style_text_align(s_ui.detail_label, LV_TEXT_ALIGN_RIGHT, 0);

    s_ui.message_area = lv_obj_create(screen);
    if (NULL == s_ui.message_area) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_pos(s_ui.message_area, OFDM_UI_TEXT_X, OFDM_UI_TEXT_Y);
    lv_obj_set_size(s_ui.message_area, OFDM_UI_TEXT_WIDTH,
                    OFDM_UI_TEXT_HEIGHT);
    style_surface(s_ui.message_area, color_hex(OFDM_UI_MESSAGE_BG_DEFAULT));
    lv_obj_set_style_border_width(s_ui.message_area, 1, 0);
    lv_obj_set_style_border_color(
        s_ui.message_area, color_hex(OFDM_UI_MESSAGE_BORDER_DEFAULT), 0);
    lv_obj_set_style_pad_all(s_ui.message_area, 8, 0);
    lv_obj_add_flag(s_ui.message_area, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_ui.message_area, LV_DIR_VER);
    s_ui.message_label = lv_label_create(s_ui.message_area);
    lv_obj_set_width(s_ui.message_label, OFDM_UI_TEXT_WIDTH - 18);
    lv_obj_set_style_text_font(s_ui.message_label,
                               &g_ofdm_font_noto_sans_sc_16, 0);
    lv_obj_set_style_text_color(
        s_ui.message_label, color_hex(OFDM_UI_MESSAGE_TEXT_DEFAULT), 0);
    lv_label_set_long_mode(s_ui.message_label, LV_LABEL_LONG_WRAP);

    s_ui.progress_bar = lv_bar_create(screen);
    lv_obj_set_pos(s_ui.progress_bar, 10, OFDM_UI_PROGRESS_Y);
    lv_obj_set_size(s_ui.progress_bar, 200, OFDM_UI_PROGRESS_HEIGHT);
    lv_bar_set_range(s_ui.progress_bar, 0, 100);
    lv_obj_set_style_bg_color(s_ui.progress_bar, color_hex(0x293337), 0);
    lv_obj_set_style_bg_color(s_ui.progress_bar, color_hex(0x4DBA91),
                              LV_PART_INDICATOR);
    s_ui.progress_label = create_label(
        screen, 216, OFDM_UI_PROGRESS_Y - 2, 94, 20,
        &lv_font_montserrat_16, color_hex(0xAEBABC));
    lv_obj_set_style_text_align(s_ui.progress_label, LV_TEXT_ALIGN_RIGHT, 0);

    s_ui.clear_button = lv_btn_create(screen);
    if (NULL == s_ui.clear_button) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_pos(s_ui.clear_button, OFDM_UI_CLEAR_X, OFDM_UI_CLEAR_Y);
    lv_obj_set_size(s_ui.clear_button, OFDM_UI_CLEAR_WIDTH,
                    OFDM_UI_CLEAR_HEIGHT);
    lv_obj_set_style_radius(s_ui.clear_button, 6, 0);
    lv_obj_set_style_bg_color(s_ui.clear_button, color_hex(0x465257), 0);
    lv_obj_add_event_cb(s_ui.clear_button, clear_callback, LV_EVENT_CLICKED,
                        NULL);
    s_ui.clear_icon = lv_label_create(s_ui.clear_button);
    s_ui.clear_label = lv_label_create(s_ui.clear_button);
    if (NULL == s_ui.clear_icon || NULL == s_ui.clear_label) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_style_text_font(s_ui.clear_icon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.clear_icon, color_hex(0xFFFFFF), 0);
    lv_label_set_text(s_ui.clear_icon, LV_SYMBOL_TRASH);
    lv_obj_align(s_ui.clear_icon, LV_ALIGN_LEFT_MID, 10, 0);
    lv_obj_set_style_text_font(s_ui.clear_label,
                               &g_ofdm_font_noto_sans_sc_16, 0);
    lv_obj_set_style_text_color(s_ui.clear_label, color_hex(0xFFFFFF), 0);
    lv_label_set_text(s_ui.clear_label, "清除");
    lv_obj_align(s_ui.clear_label, LV_ALIGN_RIGHT_MID, -10, 0);
    lv_obj_add_state(s_ui.clear_button, LV_STATE_DISABLED);

    s_ui.send_button = lv_btn_create(screen);
    lv_obj_set_pos(s_ui.send_button, OFDM_UI_SEND_X, OFDM_UI_SEND_Y);
    lv_obj_set_size(s_ui.send_button, OFDM_UI_SEND_WIDTH,
                    OFDM_UI_SEND_HEIGHT);
    lv_obj_set_style_radius(s_ui.send_button, 6, 0);
    lv_obj_set_style_bg_color(s_ui.send_button, color_hex(0x2F9C78), 0);
    lv_obj_add_event_cb(s_ui.send_button, send_callback, LV_EVENT_CLICKED,
                        NULL);
    s_ui.send_icon = lv_label_create(s_ui.send_button);
    lv_obj_set_style_text_font(s_ui.send_icon, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_ui.send_icon, color_hex(0xFFFFFF), 0);
    lv_label_set_text(s_ui.send_icon, LV_SYMBOL_UPLOAD);
    lv_obj_align(s_ui.send_icon, LV_ALIGN_LEFT_MID, 10, 0);
    s_ui.send_label = lv_label_create(s_ui.send_button);
    lv_obj_set_style_text_font(s_ui.send_label,
                               &g_ofdm_font_noto_sans_sc_16, 0);
    lv_obj_set_style_text_color(s_ui.send_label, color_hex(0xFFFFFF), 0);
    lv_label_set_text(s_ui.send_label, "发送");
    lv_obj_align(s_ui.send_label, LV_ALIGN_RIGHT_MID, -10, 0);

    if (NULL == s_ui.message_area || NULL == s_ui.message_label ||
        NULL == s_ui.progress_bar ||
        NULL == s_ui.progress_label || NULL == s_ui.clear_button ||
        NULL == s_ui.clear_icon || NULL == s_ui.clear_label ||
        NULL == s_ui.send_button ||
        NULL == s_ui.send_icon || NULL == s_ui.send_label) {
        return ESP_ERR_NO_MEM;
    }
    lv_label_set_text(s_ui.state_label, "BOOT");
    lv_label_set_text(s_ui.detail_label, "正在初始化");
    lv_label_set_text(s_ui.message_label, "等待接收文字...");
    lv_label_set_text(s_ui.progress_label, "0%");
    return ESP_OK;
}

esp_err_t ofdm_ui_init(bool touch_available)
{
    memset(&s_ui, 0, sizeof(s_ui));
    lv_init();
    esp_err_t result = ofdm_font_init();
    if (ESP_OK != result) {
        return result;
    }

    const size_t buffer_pixels =
        (size_t)OFDM_UI_HOR_RES * OFDM_UI_LCD_BUFFER_LINES;
    s_ui.draw_buffer_1 = heap_caps_malloc(
        buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    s_ui.draw_buffer_2 = heap_caps_malloc(
        buffer_pixels * sizeof(lv_color_t), MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == s_ui.draw_buffer_1 || NULL == s_ui.draw_buffer_2) {
        return ESP_ERR_NO_MEM;
    }
    static lv_disp_draw_buf_t draw_buffer;
    static lv_disp_drv_t display_driver;
    lv_disp_draw_buf_init(&draw_buffer, s_ui.draw_buffer_1,
                          s_ui.draw_buffer_2, buffer_pixels);
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = OFDM_UI_HOR_RES;
    display_driver.ver_res = OFDM_UI_VER_RES;
    display_driver.flush_cb = flush_callback;
    display_driver.draw_buf = &draw_buffer;
    if (NULL == lv_disp_drv_register(&display_driver)) {
        return ESP_FAIL;
    }
    if (touch_available) {
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
        .name = "ofdm_lvgl_tick",
        .skip_unhandled_events = true,
    };
    result = esp_timer_create(&timer_arguments, &s_ui.tick_timer);
    if (ESP_OK == result) {
        result = esp_timer_start_periodic(s_ui.tick_timer,
                                          OFDM_UI_TICK_MS * 1000U);
    }
    if (ESP_OK == result) {
        result = create_ui();
    }
    s_ui.initialized = ESP_OK == result;
    return result;
}

void ofdm_ui_process(void)
{
    if (s_ui.initialized) {
        (void)lv_timer_handler();
    }
}

void ofdm_ui_update(const ofdm_link_snapshot_t *snapshot)
{
    if (!s_ui.initialized || NULL == snapshot) {
        return;
    }
    lv_label_set_text(s_ui.state_label,
                      ofdm_link_state_name(snapshot->state));
    lv_label_set_text(s_ui.detail_label, snapshot->status);
    const bool snapshot_has_received_message =
        '\0' != snapshot->message[0] &&
        (OFDM_LINK_STATE_RX_OK == snapshot->state ||
         OFDM_LINK_STATE_IDLE_RX == snapshot->state);
    if (snapshot_has_received_message &&
        OFDM_LINK_STATE_RX_OK == snapshot->state &&
        0U != snapshot->session_id &&
        (snapshot->session_id != s_ui.last_received_session_id ||
         OFDM_LINK_STATE_RX_OK != s_ui.last_state)) {
        s_ui.last_received_session_id = snapshot->session_id;
        s_ui.message_cleared = false;
    }
    const bool received_message =
        snapshot_has_received_message && !s_ui.message_cleared;
    s_ui.message_visible = received_message;
    s_ui.last_state = snapshot->state;
    lv_label_set_text(
        s_ui.message_label,
        received_message ? snapshot->message
                         : message_placeholder(snapshot->state));
    style_message_result(received_message);
    lv_bar_set_value(s_ui.progress_bar, snapshot->progress_percent,
                     LV_ANIM_OFF);
    char progress[32] = {0};
    if (0U < snapshot->frame_count) {
        (void)snprintf(progress, sizeof(progress), "%u/%u  %u%%",
                       (unsigned int)snapshot->frame_index,
                       (unsigned int)snapshot->frame_count,
                       (unsigned int)snapshot->progress_percent);
    } else if (received_message) {
        (void)snprintf(progress, sizeof(progress), "%u bytes OK",
                       (unsigned int)snapshot->message_bytes);
    } else {
        (void)snprintf(progress, sizeof(progress), "0 bytes");
    }
    lv_label_set_text(s_ui.progress_label, progress);
    ESP_LOGI(TAG, "OFDM_UI state=%s content=%s bytes=%u",
             ofdm_link_state_name(snapshot->state),
             received_message ? "RX_MESSAGE" : "STATUS",
             (unsigned int)(received_message ? snapshot->message_bytes : 0U));

    const bool busy = OFDM_LINK_STATE_TX_DATA == snapshot->state ||
                      OFDM_LINK_STATE_RX_SYNC == snapshot->state ||
                      OFDM_LINK_STATE_RX_DATA == snapshot->state;
    if (busy) {
        lv_obj_add_state(s_ui.send_button, LV_STATE_DISABLED);
    } else {
        lv_obj_clear_state(s_ui.send_button, LV_STATE_DISABLED);
    }
    if (received_message) {
        lv_obj_clear_state(s_ui.clear_button, LV_STATE_DISABLED);
    } else {
        lv_obj_add_state(s_ui.clear_button, LV_STATE_DISABLED);
    }
}

uint32_t ofdm_ui_get_lcd_error_count(void)
{
    return s_ui.lcd_errors;
}

uint32_t ofdm_ui_get_touch_error_count(void)
{
    return s_ui.touch_errors;
}
