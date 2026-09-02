#include "imu_6axis_demo_ui.h"

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

#include <math.h>
#include <stdio.h>

static const char *TAG = "imu_6axis_demo_ui";

#define UI_HOR_RES                BOARD_LAIWFS300_LCD_V_RES
#define UI_VER_RES                BOARD_LAIWFS300_LCD_H_RES
#define UI_BUFFER_ROWS            BOARD_LAIWFS300_LCD_DRAW_BUF_LINES
#define UI_TICK_MS                2U
#define UI_TASK_DELAY_MS          10U
#define UI_TASK_STACK             8192U
#define UI_TASK_PRIORITY          2U
#define UI_TASK_CORE              1U
#define UI_FIRST_FRAME_TIMEOUT_MS 200U
#define UI_MUTEX_TIMEOUT_MS       1000U
#define UI_BAR_COUNT              6U
#define UI_BAR_WIDTH              118
#define UI_BAR_HEIGHT             8
#define UI_GRAVITY_CENTER         68
#define UI_GRAVITY_RANGE          44.0f
#define UI_GYRO_ARC_MAX_DPS       180.0f

#define UI_BG                     lv_color_make(10, 18, 28)
#define UI_PANEL                  lv_color_make(21, 32, 46)
#define UI_PANEL_ALT              lv_color_make(28, 42, 58)
#define UI_TEXT                   lv_color_make(228, 236, 244)
#define UI_MUTED                  lv_color_make(143, 160, 177)
#define UI_ACC_COLOR              lv_color_make(61, 188, 255)
#define UI_GYRO_COLOR             lv_color_make(255, 178, 72)
#define UI_X_COLOR                lv_color_make(247, 91, 91)
#define UI_Y_COLOR                lv_color_make(91, 221, 139)
#define UI_Z_COLOR                lv_color_make(151, 126, 255)
#define UI_OK_COLOR               lv_color_make(91, 221, 139)
#define UI_WARN_COLOR             lv_color_make(255, 178, 72)
#define UI_ERROR_COLOR            lv_color_make(255, 91, 111)

static SemaphoreHandle_t s_lvgl_mutex;
static lv_color_t *s_lvgl_buf1;
static lv_color_t *s_lvgl_buf2;
static esp_timer_handle_t s_lvgl_tick_timer;
static bool s_ui_ready;
static bool s_data_page;
static imu_demo_ui_callbacks_t s_callbacks;

static lv_obj_t *s_page_title;
static lv_obj_t *s_motion_badge;
static lv_obj_t *s_motion_label;
static lv_obj_t *s_attitude_panel;
static lv_obj_t *s_gravity_ball;
static lv_obj_t *s_cube;
static lv_obj_t *s_gyro_arc;
static lv_obj_t *s_roll_label;
static lv_obj_t *s_pitch_label;
static lv_obj_t *s_yaw_label;
static lv_obj_t *s_acc_magnitude_label;
static lv_obj_t *s_gyro_magnitude_label;
static lv_obj_t *s_main_status_label;
static lv_obj_t *s_main_button;
static lv_obj_t *s_main_button_label;
static lv_obj_t *s_main_cal_button;

static lv_obj_t *s_data_page_root;
static lv_obj_t *s_data_value_labels[UI_BAR_COUNT];
static lv_obj_t *s_data_bars[UI_BAR_COUNT];
static lv_obj_t *s_data_status_label;
static lv_obj_t *s_data_button;
static lv_obj_t *s_data_button_label;
static lv_obj_t *s_data_cal_button;

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    uint32_t pixel_count = (uint32_t)(area->x2 - area->x1 + 1) *
                           (uint32_t)(area->y2 - area->y1 + 1);
    uint16_t *pixels = (uint16_t *)color_map;

    (void)drv;

    for (uint32_t i = 0; i < pixel_count; i++) {
        uint16_t pixel = pixels[i];
        uint16_t red = (pixel >> 11) & 0x1FU;
        uint16_t green = (pixel >> 5) & 0x3FU;
        uint16_t blue = pixel & 0x1FU;
        uint16_t bgr = (uint16_t)((blue << 11) | (green << 5) | red);

        pixels[i] = (uint16_t)((bgr >> 8) | (bgr << 8));
    }

    (void)display_hal_draw_bitmap_rgb565(area->x1,
                                         area->y1,
                                         area->x2 - area->x1 + 1,
                                         area->y2 - area->y1 + 1,
                                         pixels);
    lv_disp_flush_ready(drv);
}

static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    static lv_coord_t last_x;
    static lv_coord_t last_y;
    touch_panel_point_t point = {0};
    uint8_t count = 0U;
    esp_err_t ret = ESP_OK;

    (void)drv;

    ret = touch_panel_read_point(&point, &count);
    if (ESP_OK == ret && 0U < count && 2U >= count) {
        last_x = (lv_coord_t)(BOARD_LAIWFS300_LCD_V_RES - 1U - point.y);
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

static lv_obj_t *create_label(lv_obj_t *parent,
                              const char *text,
                              lv_coord_t x,
                              lv_coord_t y,
                              lv_coord_t width,
                              const lv_font_t *font,
                              lv_color_t color)
{
    lv_obj_t *label = lv_label_create(parent);

    if (NULL == label) {
        return NULL;
    }
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_width(label, width);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_CLIP);
    return label;
}

static void style_button(lv_obj_t *button, lv_color_t color)
{
    lv_obj_set_style_radius(button, 6, 0);
    lv_obj_set_style_shadow_width(button, 0, 0);
    lv_obj_set_style_bg_color(button, color, 0);
    lv_obj_set_style_bg_opa(button, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_make(70, 88, 108), 0);
    lv_obj_set_style_pad_all(button, 4, 0);
}

static lv_obj_t *create_button(lv_obj_t *parent,
                               const char *text,
                               lv_coord_t x,
                               lv_coord_t y,
                               lv_coord_t width,
                               lv_event_cb_t event_cb)
{
    lv_obj_t *button = lv_btn_create(parent);
    lv_obj_t *label = NULL;

    if (NULL == button) {
        return NULL;
    }
    lv_obj_set_size(button, width, 30);
    lv_obj_set_pos(button, x, y);
    style_button(button, UI_PANEL_ALT);
    lv_obj_add_event_cb(button, event_cb, LV_EVENT_CLICKED, NULL);

    label = lv_label_create(button);
    if (NULL == label) {
        return button;
    }
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(label, UI_TEXT, 0);
    lv_obj_center(label);
    return button;
}

static void style_panel(lv_obj_t *panel, lv_color_t color)
{
    lv_obj_set_style_radius(panel, 10, 0);
    lv_obj_set_style_bg_color(panel, color, 0);
    lv_obj_set_style_bg_opa(panel, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(panel, 1, 0);
    lv_obj_set_style_border_color(panel, lv_color_make(53, 72, 94), 0);
    lv_obj_set_style_shadow_width(panel, 0, 0);
    lv_obj_set_style_pad_all(panel, 0, 0);
    lv_obj_clear_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
}

static lv_obj_t *create_value_row(lv_obj_t *parent,
                                  const char *name,
                                  lv_coord_t base_x,
                                  lv_coord_t y,
                                  lv_color_t color,
                                  int32_t min_value,
                                  int32_t max_value)
{
    lv_obj_t *name_label = NULL;
    lv_obj_t *bar = NULL;

    name_label = create_label(parent, name, base_x, y - 5, 16,
                              &lv_font_montserrat_14, color);
    if (NULL == name_label) {
        return NULL;
    }
    bar = lv_bar_create(parent);
    if (NULL == bar) {
        return NULL;
    }
    lv_obj_set_size(bar, UI_BAR_WIDTH, UI_BAR_HEIGHT);
    lv_obj_set_pos(bar, base_x + 16, y);
    lv_bar_set_range(bar, min_value, max_value);
    lv_bar_set_value(bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(bar, 4, LV_PART_MAIN);
    lv_obj_set_style_radius(bar, 4, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(bar, lv_color_make(37, 53, 70), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_bg_color(bar, color, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_INDICATOR);
    return bar;
}

static float clamp_float(float value, float min_value, float max_value)
{
    if (value < min_value) {
        return min_value;
    }
    if (value > max_value) {
        return max_value;
    }
    return value;
}

static lv_color_t motion_color(imu_demo_motion_t motion)
{
    switch (motion) {
    case IMU_DEMO_MOTION_TILTED:
        return UI_WARN_COLOR;
    case IMU_DEMO_MOTION_ROTATING:
    case IMU_DEMO_MOTION_SHAKING:
        return UI_ERROR_COLOR;
    case IMU_DEMO_MOTION_STILL:
    default:
        return UI_OK_COLOR;
    }
}

static void ui_toggle_page_cb(lv_event_t *event)
{
    if (NULL == event || LV_EVENT_CLICKED != lv_event_get_code(event)) {
        return;
    }

    s_data_page = !s_data_page;
    if (s_data_page) {
        lv_obj_clear_flag(s_data_page_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_attitude_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_roll_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_pitch_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_yaw_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_acc_magnitude_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_gyro_magnitude_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_main_status_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_main_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_flag(s_main_cal_button, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_page_title, "SENSOR DATA");
        lv_label_set_text(s_data_button_label, LV_SYMBOL_LEFT " VIEW");
    } else {
        lv_obj_add_flag(s_data_page_root, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_attitude_panel, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_roll_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_pitch_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_yaw_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_acc_magnitude_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_gyro_magnitude_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_main_status_label, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_main_button, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_flag(s_main_cal_button, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(s_page_title, "IMU MOTION LAB");
        lv_label_set_text(s_main_button_label, LV_SYMBOL_RIGHT " DATA");
    }
}

static void ui_calibrate_cb(lv_event_t *event)
{
    if (NULL == event || LV_EVENT_CLICKED != lv_event_get_code(event)) {
        return;
    }
    if (NULL != s_callbacks.on_calibrate) {
        s_callbacks.on_calibrate(s_callbacks.user_ctx);
    }
}

static esp_err_t ui_init_screen(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_t *label = NULL;
    lv_obj_t *line = NULL;
    lv_obj_t *data_title = NULL;

    lv_obj_set_style_bg_color(screen, UI_BG, 0);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, 0);

    s_page_title = create_label(screen, "IMU MOTION LAB", 16, 8, 190,
                                 &lv_font_montserrat_20, UI_TEXT);
    s_motion_badge = lv_obj_create(screen);
    if (NULL == s_page_title || NULL == s_motion_badge) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_size(s_motion_badge, 100, 26);
    lv_obj_set_pos(s_motion_badge, 204, 7);
    style_panel(s_motion_badge, UI_PANEL_ALT);
    s_motion_label = create_label(s_motion_badge, "STILL", 0, 3, 100,
                                  &lv_font_montserrat_14, UI_OK_COLOR);
    lv_obj_set_style_text_align(s_motion_label, LV_TEXT_ALIGN_CENTER, 0);

    s_attitude_panel = lv_obj_create(screen);
    if (NULL == s_attitude_panel) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_size(s_attitude_panel, 138, 138);
    lv_obj_set_pos(s_attitude_panel, 12, 40);
    style_panel(s_attitude_panel, UI_PANEL);

    line = lv_obj_create(s_attitude_panel);
    lv_obj_set_size(line, 106, 1);
    lv_obj_set_pos(line, 16, 68);
    lv_obj_set_style_bg_color(line, lv_color_make(67, 91, 115), 0);
    lv_obj_set_style_border_width(line, 0, 0);
    line = lv_obj_create(s_attitude_panel);
    lv_obj_set_size(line, 1, 106);
    lv_obj_set_pos(line, 68, 16);
    lv_obj_set_style_bg_color(line, lv_color_make(67, 91, 115), 0);
    lv_obj_set_style_border_width(line, 0, 0);

    s_gyro_arc = lv_arc_create(s_attitude_panel);
    if (NULL == s_gyro_arc) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_size(s_gyro_arc, 126, 126);
    lv_obj_set_pos(s_gyro_arc, 6, 6);
    lv_arc_set_bg_angles(s_gyro_arc, 0, 360);
    lv_arc_set_range(s_gyro_arc, 0, 100);
    lv_arc_set_value(s_gyro_arc, 0);
    lv_obj_remove_style(s_gyro_arc, NULL, LV_PART_KNOB);
    lv_obj_set_style_arc_width(s_gyro_arc, 3, LV_PART_MAIN);
    lv_obj_set_style_arc_color(s_gyro_arc, lv_color_make(39, 55, 72), LV_PART_MAIN);
    lv_obj_set_style_arc_width(s_gyro_arc, 5, LV_PART_INDICATOR);
    lv_obj_set_style_arc_color(s_gyro_arc, UI_GYRO_COLOR, LV_PART_INDICATOR);
    lv_obj_set_style_bg_opa(s_gyro_arc, LV_OPA_TRANSP, LV_PART_MAIN);

    s_cube = lv_obj_create(s_attitude_panel);
    if (NULL == s_cube) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_size(s_cube, 68, 68);
    lv_obj_set_pos(s_cube, 34, 34);
    lv_obj_set_style_bg_color(s_cube, lv_color_make(35, 74, 111), 0);
    lv_obj_set_style_bg_opa(s_cube, LV_OPA_70, 0);
    lv_obj_set_style_border_width(s_cube, 2, 0);
    lv_obj_set_style_border_color(s_cube, UI_ACC_COLOR, 0);
    lv_obj_set_style_radius(s_cube, 4, 0);
    lv_obj_set_style_transform_pivot_x(s_cube, 34, 0);
    lv_obj_set_style_transform_pivot_y(s_cube, 34, 0);
    lv_obj_set_style_transform_angle(s_cube, 0, 0);
    lv_obj_clear_flag(s_cube, LV_OBJ_FLAG_SCROLLABLE);
    label = create_label(s_cube, "XYZ", 0, 23, 68, &lv_font_montserrat_14, UI_TEXT);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);

    s_gravity_ball = lv_obj_create(s_attitude_panel);
    if (NULL == s_gravity_ball) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_size(s_gravity_ball, 14, 14);
    lv_obj_set_pos(s_gravity_ball, 61, 61);
    lv_obj_set_style_radius(s_gravity_ball, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(s_gravity_ball, UI_OK_COLOR, 0);
    lv_obj_set_style_border_width(s_gravity_ball, 2, 0);
    lv_obj_set_style_border_color(s_gravity_ball, UI_TEXT, 0);

    s_roll_label = create_label(screen, "ROLL  +0.0 deg", 162, 46, 145,
                                &lv_font_montserrat_14, UI_X_COLOR);
    s_pitch_label = create_label(screen, "PITCH +0.0 deg", 162, 67, 145,
                                 &lv_font_montserrat_14, UI_Y_COLOR);
    s_yaw_label = create_label(screen, "YAW   +0.0 deg", 162, 88, 145,
                               &lv_font_montserrat_14, UI_Z_COLOR);
    s_acc_magnitude_label = create_label(screen, "ACC  1.00g", 162, 112, 145,
                                         &lv_font_montserrat_14, UI_ACC_COLOR);
    s_gyro_magnitude_label = create_label(screen, "GYRO 0.0 deg/s", 162, 133, 145,
                                          &lv_font_montserrat_14, UI_GYRO_COLOR);
    s_main_status_label = create_label(screen, "Gravity vector stable", 162, 157, 145,
                                       &lv_font_montserrat_14, UI_MUTED);

    s_main_button = create_button(screen, LV_SYMBOL_RIGHT " DATA", 162, 201, 88,
                                  ui_toggle_page_cb);
    s_main_cal_button = create_button(screen, LV_SYMBOL_REFRESH " CAL", 254, 201, 54,
                                      ui_calibrate_cb);
    if (NULL == s_roll_label || NULL == s_pitch_label || NULL == s_yaw_label ||
        NULL == s_acc_magnitude_label || NULL == s_gyro_magnitude_label ||
        NULL == s_main_status_label || NULL == s_main_button || NULL == s_main_cal_button) {
        return ESP_ERR_NO_MEM;
    }
    s_main_button_label = lv_obj_get_child(s_main_button, 0);

    s_data_page_root = lv_obj_create(screen);
    if (NULL == s_data_page_root) {
        return ESP_ERR_NO_MEM;
    }
    lv_obj_set_size(s_data_page_root, UI_HOR_RES, UI_VER_RES);
    lv_obj_set_pos(s_data_page_root, 0, 0);
    lv_obj_set_style_bg_color(s_data_page_root, UI_BG, 0);
    lv_obj_set_style_bg_opa(s_data_page_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_data_page_root, 0, 0);
    lv_obj_clear_flag(s_data_page_root, LV_OBJ_FLAG_SCROLLABLE);

    data_title = create_label(s_data_page_root, "ACCELERATION", 18, 43, 145,
                              &lv_font_montserrat_14, UI_ACC_COLOR);
    label = create_label(s_data_page_root, "GYROSCOPE", 178, 43, 125,
                         &lv_font_montserrat_14, UI_GYRO_COLOR);
    if (NULL == data_title || NULL == label) {
        return ESP_ERR_NO_MEM;
    }

    s_data_bars[0] = create_value_row(s_data_page_root, "X", 18, 68, UI_X_COLOR, -200, 200);
    s_data_bars[1] = create_value_row(s_data_page_root, "Y", 18, 99, UI_Y_COLOR, -200, 200);
    s_data_bars[2] = create_value_row(s_data_page_root, "Z", 18, 130, UI_Z_COLOR, -200, 200);
    s_data_bars[3] = create_value_row(s_data_page_root, "X", 178, 68, UI_X_COLOR, -180, 180);
    s_data_bars[4] = create_value_row(s_data_page_root, "Y", 178, 99, UI_Y_COLOR, -180, 180);
    s_data_bars[5] = create_value_row(s_data_page_root, "Z", 178, 130, UI_Z_COLOR, -180, 180);
    for (uint32_t i = 0U; i < UI_BAR_COUNT; i++) {
        if (NULL == s_data_bars[i]) {
            return ESP_ERR_NO_MEM;
        }
        s_data_value_labels[i] = create_label(s_data_page_root, "0.00", (i < 3U) ? 34 : 194,
                                              (lv_coord_t)(s_data_bars[i] == s_data_bars[0] ||
                                                                   s_data_bars[i] == s_data_bars[3]
                                                               ? 78
                                                               : (s_data_bars[i] == s_data_bars[1] ||
                                                                          s_data_bars[i] == s_data_bars[4]
                                                                      ? 109
                                                                      : 140)),
                                              118, &lv_font_montserrat_14, UI_TEXT);
        if (NULL == s_data_value_labels[i]) {
            return ESP_ERR_NO_MEM;
        }
    }

    s_data_status_label = create_label(s_data_page_root, "Hold still, then tap CAL", 18, 171, 285,
                                       &lv_font_montserrat_14, UI_MUTED);
    s_data_button = create_button(s_data_page_root, LV_SYMBOL_LEFT " VIEW", 162, 201, 88,
                                  ui_toggle_page_cb);
    s_data_cal_button = create_button(s_data_page_root, LV_SYMBOL_REFRESH " CAL", 254, 201, 54,
                                      ui_calibrate_cb);
    if (NULL == s_data_status_label || NULL == s_data_button || NULL == s_data_cal_button) {
        return ESP_ERR_NO_MEM;
    }
    s_data_button_label = lv_obj_get_child(s_data_button, 0);
    lv_obj_add_flag(s_data_page_root, LV_OBJ_FLAG_HIDDEN);
    return ESP_OK;
}

static esp_err_t ui_present_first_frame(void)
{
    lv_timer_handler();
    return display_hal_wait_pending(UI_FIRST_FRAME_TIMEOUT_MS);
}

static void update_main_page(const imu_demo_status_t *status)
{
    char text[32] = {0};
    float ball_x = 0.0f;
    float ball_y = 0.0f;
    int32_t arc_value = 0;
    lv_color_t state_color = UI_MUTED;

    ball_x = clamp_float(-status->sample.accel_y_g * UI_GRAVITY_RANGE,
                         -UI_GRAVITY_RANGE, UI_GRAVITY_RANGE);
    ball_y = clamp_float(-status->sample.accel_x_g * UI_GRAVITY_RANGE,
                         -UI_GRAVITY_RANGE, UI_GRAVITY_RANGE);
    lv_obj_set_pos(s_gravity_ball,
                   (lv_coord_t)(UI_GRAVITY_CENTER - 7 + ball_x),
                   (lv_coord_t)(UI_GRAVITY_CENTER - 7 + ball_y));
    lv_obj_set_style_transform_angle(s_cube,
                                     (int32_t)(-status->sample.roll_deg * 10.0f), 0);

    snprintf(text, sizeof(text), "ROLL  %+5.1f deg", (double)status->sample.roll_deg);
    lv_label_set_text(s_roll_label, text);
    snprintf(text, sizeof(text), "PITCH %+5.1f deg", (double)status->sample.pitch_deg);
    lv_label_set_text(s_pitch_label, text);
    snprintf(text, sizeof(text), "YAW   %+5.1f deg", (double)status->sample.yaw_deg);
    lv_label_set_text(s_yaw_label, text);
    snprintf(text, sizeof(text), "ACC  %4.2fg", (double)status->sample.accel_magnitude_g);
    lv_label_set_text(s_acc_magnitude_label, text);
    snprintf(text, sizeof(text), "GYRO %4.1f deg/s", (double)status->sample.gyro_magnitude_dps);
    lv_label_set_text(s_gyro_magnitude_label, text);

    if (status->calibrating) {
        snprintf(text, sizeof(text), "Calibrating %u%%", status->calibration_percent);
        state_color = UI_WARN_COLOR;
    } else if (!status->sensor_ok) {
        snprintf(text, sizeof(text), "Sensor error");
        state_color = UI_ERROR_COLOR;
    } else {
        snprintf(text, sizeof(text), "%s", imu_demo_motion_text(status->sample.motion));
        state_color = motion_color(status->sample.motion);
    }
    lv_label_set_text(s_main_status_label, text);
    lv_obj_set_style_text_color(s_main_status_label, state_color, 0);
    lv_label_set_text(s_motion_label, status->calibrating ? "CAL" :
                                      imu_demo_motion_text(status->sample.motion));
    lv_obj_set_style_text_color(s_motion_label, state_color, 0);
    lv_obj_set_style_border_color(s_motion_badge, state_color, 0);

    arc_value = (int32_t)clamp_float((status->sample.gyro_magnitude_dps /
                                      UI_GYRO_ARC_MAX_DPS) * 100.0f,
                                     0.0f, 100.0f);
    lv_arc_set_value(s_gyro_arc, arc_value);
}

static void update_data_page(const imu_demo_status_t *status)
{
    const float values[UI_BAR_COUNT] = {
        status->sample.accel_x_g,
        status->sample.accel_y_g,
        status->sample.accel_z_g,
        status->sample.gyro_x_dps,
        status->sample.gyro_y_dps,
        status->sample.gyro_z_dps,
    };
    char text[40] = {0};

    for (uint32_t i = 0U; i < UI_BAR_COUNT; i++) {
        snprintf(text, sizeof(text), (i < 3U) ? "%+.2f" : "%+.1f", (double)values[i]);
        lv_label_set_text(s_data_value_labels[i], text);
        lv_bar_set_value(s_data_bars[i],
                         (int32_t)((i < 3U) ? values[i] * 100.0f : values[i]),
                         LV_ANIM_OFF);
    }
    if (status->calibrating) {
        snprintf(text, sizeof(text), "Calibrating gyro %u%%", status->calibration_percent);
    } else if (!status->sensor_ok) {
        snprintf(text, sizeof(text), "Sensor error");
    } else {
        snprintf(text, sizeof(text), "Relative yaw: %+d deg", (int)status->sample.yaw_deg);
    }
    lv_label_set_text(s_data_status_label, text);
}

esp_err_t imu_demo_ui_init(const imu_demo_ui_callbacks_t *callbacks)
{
    static lv_disp_draw_buf_t draw_buf;
    static lv_disp_drv_t disp_drv;
    static lv_indev_drv_t indev_drv;
    bool touch_ok = false;
    size_t buffer_pixels = 0U;
    BaseType_t task_ok = pdFAIL;
    esp_err_t ret = ESP_OK;

    if (NULL == callbacks || NULL == callbacks->on_calibrate) {
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
            .name = "imu_lvgl_tick",
            .skip_unhandled_events = true,
        };
        ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &s_lvgl_tick_timer),
                            TAG, "tick timer create failed");
        ESP_RETURN_ON_ERROR(esp_timer_start_periodic(s_lvgl_tick_timer, UI_TICK_MS * 1000U),
                            TAG, "tick timer start failed");
    }

    task_ok = xTaskCreatePinnedToCore(lvgl_task, "imu_lvgl", UI_TASK_STACK, NULL,
                                      UI_TASK_PRIORITY, NULL, UI_TASK_CORE);
    if (pdPASS != task_ok) {
        return ESP_ERR_NO_MEM;
    }

    s_ui_ready = true;
    ESP_LOGI(TAG, "IMU UI initialized: %dx%d%s", UI_HOR_RES, UI_VER_RES,
             touch_ok ? " with touch" : " without touch");
    return ESP_OK;
}

esp_err_t imu_demo_ui_update(const imu_demo_status_t *status)
{
    if (NULL == status) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ui_ready || NULL == s_lvgl_mutex) {
        return ESP_ERR_INVALID_STATE;
    }
    if (pdTRUE != xSemaphoreTakeRecursive(s_lvgl_mutex, pdMS_TO_TICKS(UI_MUTEX_TIMEOUT_MS))) {
        return ESP_ERR_TIMEOUT;
    }

    if (s_data_page) {
        update_data_page(status);
    } else {
        update_main_page(status);
    }
    xSemaphoreGiveRecursive(s_lvgl_mutex);
    return ESP_OK;
}
