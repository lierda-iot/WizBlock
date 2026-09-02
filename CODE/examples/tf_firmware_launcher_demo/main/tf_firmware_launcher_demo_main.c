#include "firmware_package.h"
#include "launcher_boot.h"

#include "board_laiwfs300.h"
#include "board_pins.h"
#include "display_hal.h"
#include "io_expander.h"
#include "launcher_return.h"
#include "storage_hal.h"
#include "touch_hal.h"

#include "esp_heap_caps.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "tf_firmware_launcher";

#define TF_MOUNT_POINT "/tfcard"
#define TF_PACKAGES_ROOT TF_MOUNT_POINT "/demo_hub/packages"
#define TF_BOOT_ANIMATION_PATH TF_MOUNT_POINT "/demo_hub/launcher/boot_animation.r565z"
#define TF_BOOT_AUDIO_PATH TF_MOUNT_POINT "/demo_hub/launcher/boot_sound.pcm"
#define TF_SPI_MAX_FREQ_KHZ 10000
#define TF_MAX_OPEN_FILES 8
#define TF_ALLOCATION_UNIT_SIZE (16U * 1024U)

#define LCD_H_RES BOARD_LAIWFS300_LCD_H_RES
#define LCD_V_RES BOARD_LAIWFS300_LCD_V_RES
#define LCD_PIXEL_CLOCK_HZ 40000000U
#define LCD_DRAW_BUFFER_LINES 80U
#define LVGL_HOR_RES LCD_V_RES
#define LVGL_VER_RES LCD_H_RES
#define LVGL_BUFFER_ROWS LCD_DRAW_BUFFER_LINES
#define LVGL_TICK_MS 2
#define LVGL_TASK_DELAY_MS 10
#define LVGL_TASK_STACK 8192
#define LVGL_TASK_PRIORITY 2
#define LVGL_TASK_CORE 1
#define LAUNCHER_WORKER_STACK 8192
#define LAUNCHER_WORKER_PRIORITY 4
#define LAUNCHER_STATUS_MAX 96
#define INSTALL_STAGE_MAX 32
#define INSTALL_UI_INITIAL_RENDER_MS 150
#define INSTALL_UI_PROGRESS_RENDER_MS 20
#define INSTALL_RESTART_DELAY_MS 1200
#define CAROUSEL_HEIGHT 126
#define PACKAGE_CARD_WIDTH 232
#define PACKAGE_CARD_HEIGHT 108
#define PACKAGE_CARD_GAP 12
#define PACKAGE_CARD_SIDE_PADDING ((LVGL_HOR_RES - PACKAGE_CARD_WIDTH) / 2)
#define PACKAGE_CARD_ICON_SIZE 42
#define PAGE_DOT_SIZE 6

typedef struct {
    char status[LAUNCHER_STATUS_MAX];
    size_t package_count;
    size_t rejected_count;
    int selected_index;
    int progress;
    bool busy;
    uint32_t catalog_generation;
} launcher_state_t;

typedef struct {
    char last_stage[INSTALL_STAGE_MAX];
    int last_percent;
    bool spi_mutex_held;
} install_progress_context_t;

static SemaphoreHandle_t s_lvgl_mutex;
static SemaphoreHandle_t s_state_mutex;
static SemaphoreHandle_t s_spi2_mutex;
static launcher_state_t s_state;
static firmware_package_t s_packages[FIRMWARE_PACKAGE_MAX_COUNT];

static lv_obj_t *s_status_label;
static lv_obj_t *s_package_carousel;
static lv_obj_t *s_selected_label;
static lv_obj_t *s_page_dots_container;
static lv_obj_t *s_package_cards[FIRMWARE_PACKAGE_MAX_COUNT];
static lv_obj_t *s_page_dots[FIRMWARE_PACKAGE_MAX_COUNT];
static lv_obj_t *s_progress_bar;
static lv_obj_t *s_refresh_button;
static lv_obj_t *s_run_button;
static uint32_t s_rendered_generation;
static char s_rendered_status[LAUNCHER_STATUS_MAX];
static int s_rendered_progress = -1;
static bool s_rendered_busy;

static void set_status(const char *status, int progress, bool busy)
{
    if (NULL == status || NULL == s_state_mutex) {
        return;
    }
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    snprintf(s_state.status, sizeof(s_state.status), "%s", status);
    s_state.progress = progress;
    s_state.busy = busy;
    xSemaphoreGive(s_state_mutex);
}

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_map)
{
    uint32_t pixel_count = (uint32_t)(area->x2 - area->x1 + 1) *
                           (uint32_t)(area->y2 - area->y1 + 1);
    uint16_t *pixels = (uint16_t *)color_map;
    for (uint32_t index = 0; index < pixel_count; ++index) {
        uint16_t pixel = pixels[index];
        uint16_t red = (pixel >> 11) & 0x1f;
        uint16_t green = (pixel >> 5) & 0x3f;
        uint16_t blue = pixel & 0x1f;
        uint16_t bgr = (blue << 11) | (green << 5) | red;
        pixels[index] = (bgr >> 8) | (bgr << 8);
    }

    xSemaphoreTake(s_spi2_mutex, portMAX_DELAY);
    esp_err_t ret = display_hal_draw_bitmap_rgb565(area->x1, area->y1,
                                                    area->x2 - area->x1 + 1,
                                                    area->y2 - area->y1 + 1,
                                                    pixels);
    if (ESP_OK == ret) {
        ret = display_hal_wait_pending(1000);
    }
    xSemaphoreGive(s_spi2_mutex);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "display flush failed: %s", esp_err_to_name(ret));
    }
    lv_disp_flush_ready(drv);
}

static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    static lv_coord_t last_x;
    static lv_coord_t last_y;

    touch_panel_point_t point = {0};
    uint8_t touch_count = 0;
    esp_err_t ret = touch_panel_read_point(&point, &touch_count);
    if (ESP_OK == ret && touch_count > 0 && touch_count <= 2) {
        last_x = (lv_coord_t)(LCD_V_RES - 1 - point.y);
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
    lv_tick_inc(LVGL_TICK_MS);
}

static const char *package_icon_for_id(const char *package_id)
{
    if (NULL == package_id) {
        return LV_SYMBOL_FILE;
    }
    if (0 == strncmp(package_id, "audio", 5)) {
        return LV_SYMBOL_AUDIO;
    }
    if (0 == strncmp(package_id, "imu", 3)) {
        return LV_SYMBOL_GPS;
    }
    if (0 == strncmp(package_id, "lte", 3)) {
        return LV_SYMBOL_WIFI;
    }
    if (0 == strncmp(package_id, "salary", 6)) {
        return LV_SYMBOL_CHARGE;
    }
    return LV_SYMBOL_FILE;
}

static lv_color_t package_accent_for_id(const char *package_id)
{
    if (NULL != package_id && 0 == strncmp(package_id, "audio", 5)) {
        return lv_color_hex(0x0f766e);
    }
    if (NULL != package_id && 0 == strncmp(package_id, "imu", 3)) {
        return lv_color_hex(0x2563eb);
    }
    if (NULL != package_id && 0 == strncmp(package_id, "lte", 3)) {
        return lv_color_hex(0x15803d);
    }
    if (NULL != package_id && 0 == strncmp(package_id, "salary", 6)) {
        return lv_color_hex(0xb45309);
    }
    return lv_color_hex(0x475569);
}

static void rebuild_page_dots(size_t package_count)
{
    if (NULL == s_page_dots_container) {
        return;
    }

    for (size_t index = 0; index < FIRMWARE_PACKAGE_MAX_COUNT; ++index) {
        if (NULL != s_page_dots[index]) {
            lv_obj_del(s_page_dots[index]);
            s_page_dots[index] = NULL;
        }
    }

    if (0 == package_count) {
        lv_obj_add_flag(s_page_dots_container, LV_OBJ_FLAG_HIDDEN);
        return;
    }

    lv_obj_clear_flag(s_page_dots_container, LV_OBJ_FLAG_HIDDEN);
    for (size_t index = 0; index < package_count && index < FIRMWARE_PACKAGE_MAX_COUNT; ++index) {
        s_page_dots[index] = lv_obj_create(s_page_dots_container);
        lv_obj_set_size(s_page_dots[index], PAGE_DOT_SIZE, PAGE_DOT_SIZE);
        lv_obj_set_style_radius(s_page_dots[index], LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_border_width(s_page_dots[index], 0, 0);
        lv_obj_set_style_bg_color(s_page_dots[index], lv_color_hex(0xcbd5e1), 0);
        lv_obj_clear_flag(s_page_dots[index], LV_OBJ_FLAG_CLICKABLE);
        lv_obj_clear_flag(s_page_dots[index], LV_OBJ_FLAG_SCROLLABLE);
    }
}

static void update_page_dots(size_t active_index, size_t package_count)
{
    for (size_t index = 0; index < package_count && index < FIRMWARE_PACKAGE_MAX_COUNT; ++index) {
        if (NULL != s_page_dots[index]) {
            lv_obj_set_style_bg_color(s_page_dots[index],
                                      (index == active_index) ? lv_color_hex(0x0f766e) :
                                                                 lv_color_hex(0xcbd5e1),
                                      0);
        }
    }
}

static void apply_card_selection(size_t active_index, size_t package_count)
{
    for (size_t index = 0; index < package_count && index < FIRMWARE_PACKAGE_MAX_COUNT; ++index) {
        lv_obj_t *card = s_package_cards[index];
        if (NULL == card) {
            continue;
        }
        bool selected = (index == active_index);
        lv_obj_set_style_border_width(card, selected ? 2 : 1, 0);
        lv_obj_set_style_border_color(card, selected ? lv_color_hex(0x0f766e) :
                                                      lv_color_hex(0xd5dce5),
                                      0);
        lv_obj_set_style_bg_color(card, selected ? lv_color_hex(0xf0fdfa) :
                                                  lv_color_hex(0xffffff),
                                  0);
    }
    update_page_dots(active_index, package_count);
}

static void select_package(int package_index, bool scroll_to_card)
{
    firmware_package_t package = {0};
    size_t package_count = 0;

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    package_count = s_state.package_count;
    if (s_state.busy || package_index < 0 || (size_t)package_index >= package_count) {
        xSemaphoreGive(s_state_mutex);
        return;
    }
    s_state.selected_index = package_index;
    package = s_packages[package_index];
    xSemaphoreGive(s_state_mutex);

    char selected_text[FIRMWARE_PACKAGE_NAME_MAX + FIRMWARE_PACKAGE_VERSION_MAX + 24] = {0};
    snprintf(selected_text, sizeof(selected_text), "%s  v%s",
             package.name, package.version);
    lv_label_set_text(s_selected_label, selected_text);
    lv_obj_clear_state(s_run_button, LV_STATE_DISABLED);
    apply_card_selection((size_t)package_index, package_count);

    if (scroll_to_card && NULL != s_package_cards[package_index]) {
        lv_obj_scroll_to_view(s_package_cards[package_index], LV_ANIM_ON);
    }
}

static void package_card_event_cb(lv_event_t *event)
{
    if (LV_EVENT_CLICKED != lv_event_get_code(event)) {
        return;
    }
    select_package((int)(intptr_t)lv_event_get_user_data(event), true);
}

static void carousel_scroll_end_event_cb(lv_event_t *event)
{
    if (LV_EVENT_SCROLL_END != lv_event_get_code(event) || NULL == s_package_carousel) {
        return;
    }

    lv_area_t carousel_area = {0};
    lv_obj_get_coords(s_package_carousel, &carousel_area);
    lv_coord_t carousel_center = (carousel_area.x1 + carousel_area.x2) / 2;
    size_t package_count = 0;
    size_t nearest_index = 0;
    lv_coord_t nearest_distance = LV_COORD_MAX;

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    package_count = s_state.package_count;
    bool busy = s_state.busy;
    xSemaphoreGive(s_state_mutex);
    if (busy || 0 == package_count) {
        return;
    }

    for (size_t index = 0; index < package_count; ++index) {
        if (NULL == s_package_cards[index]) {
            continue;
        }
        lv_area_t card_area = {0};
        lv_obj_get_coords(s_package_cards[index], &card_area);
        lv_coord_t card_center = (card_area.x1 + card_area.x2) / 2;
        lv_coord_t distance = LV_ABS(card_center - carousel_center);
        if (distance < nearest_distance) {
            nearest_distance = distance;
            nearest_index = index;
        }
    }
    select_package((int)nearest_index, false);
}

static void rebuild_package_carousel(void)
{
    lv_obj_clean(s_package_carousel);
    memset(s_package_cards, 0, sizeof(s_package_cards));

    size_t package_count = 0;
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    package_count = s_state.package_count;
    xSemaphoreGive(s_state_mutex);

    if (0 == package_count) {
        lv_obj_t *empty_label = lv_label_create(s_package_carousel);
        lv_label_set_text(empty_label, "No compatible firmware packages");
        lv_obj_set_style_text_color(empty_label, lv_color_hex(0x64748b), 0);
        lv_obj_center(empty_label);
        lv_label_set_text(s_selected_label, "Insert TF card or refresh");
        rebuild_page_dots(0);
        lv_obj_add_state(s_run_button, LV_STATE_DISABLED);
        return;
    }

    rebuild_page_dots(package_count);

    for (size_t index = 0; index < package_count; ++index) {
        firmware_package_t package = {0};
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
        package = s_packages[index];
        xSemaphoreGive(s_state_mutex);

        lv_obj_t *card = lv_obj_create(s_package_carousel);
        s_package_cards[index] = card;
        lv_obj_set_size(card, PACKAGE_CARD_WIDTH, PACKAGE_CARD_HEIGHT);
        lv_obj_set_style_radius(card, 8, 0);
        lv_obj_set_style_bg_color(card, lv_color_hex(0xffffff), 0);
        lv_obj_set_style_border_width(card, 1, 0);
        lv_obj_set_style_border_color(card, lv_color_hex(0xd5dce5), 0);
        lv_obj_set_style_pad_all(card, 12, 0);
        lv_obj_set_style_pad_row(card, 3, 0);
        lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(card, LV_OBJ_FLAG_CLICKABLE);
        lv_obj_add_event_cb(card, package_card_event_cb, LV_EVENT_CLICKED,
                            (void *)(intptr_t)index);

        lv_obj_t *icon = lv_obj_create(card);
        lv_obj_set_size(icon, PACKAGE_CARD_ICON_SIZE, PACKAGE_CARD_ICON_SIZE);
        lv_obj_set_style_radius(icon, 12, 0);
        lv_obj_set_style_border_width(icon, 0, 0);
        lv_obj_set_style_bg_color(icon, package_accent_for_id(package.id), 0);
        lv_obj_clear_flag(icon, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_align(icon, LV_ALIGN_TOP_LEFT, 0, 0);

        lv_obj_t *icon_label = lv_label_create(icon);
        lv_label_set_text(icon_label, package_icon_for_id(package.id));
        lv_obj_set_style_text_color(icon_label, lv_color_hex(0xffffff), 0);
        lv_obj_center(icon_label);

        lv_obj_t *name_label = lv_label_create(card);
        lv_obj_set_width(name_label, PACKAGE_CARD_WIDTH - 24 - PACKAGE_CARD_ICON_SIZE - 10);
        lv_obj_set_height(name_label, 44);
        lv_label_set_text(name_label, package.name);
        lv_label_set_long_mode(name_label, LV_LABEL_LONG_WRAP);
        lv_obj_set_style_text_font(name_label, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(name_label, lv_color_hex(0x172033), 0);
        lv_obj_align(name_label, LV_ALIGN_TOP_LEFT, PACKAGE_CARD_ICON_SIZE + 10, 0);

        lv_obj_t *version_label = lv_label_create(card);
        char version_text[48] = {0};
        snprintf(version_text, sizeof(version_text), "v%s  |  %lu KB",
                 package.version, (unsigned long)((package.app_size + 1023U) / 1024U));
        lv_label_set_text(version_label, version_text);
        lv_obj_set_style_text_color(version_label, lv_color_hex(0x64748b), 0);
        lv_obj_align(version_label, LV_ALIGN_BOTTOM_LEFT, 0, 0);

        lv_obj_t *resource_label = lv_label_create(card);
        char resource_text[32] = {0};
        snprintf(resource_text, sizeof(resource_text), "%u resource%s",
                 (unsigned)package.resource_count, package.resource_count == 1 ? "" : "s");
        lv_label_set_text(resource_label, resource_text);
        lv_obj_set_style_text_color(resource_label, lv_color_hex(0x94a3b8), 0);
        lv_obj_align(resource_label, LV_ALIGN_BOTTOM_RIGHT, 0, 0);
    }

    lv_obj_update_layout(s_package_carousel);
    lv_obj_scroll_to_x(s_package_carousel, 0, LV_ANIM_OFF);
    select_package(0, false);
}

static void progress_callback(int percent, const char *stage, void *user_ctx)
{
    install_progress_context_t *context = (install_progress_context_t *)user_ctx;
    const char *safe_stage = (NULL != stage) ? stage : "Working";
    if (NULL != context && context->last_percent == percent &&
        0 == strcmp(context->last_stage, safe_stage)) {
        return;
    }

    if (NULL != context) {
        context->last_percent = percent;
        snprintf(context->last_stage, sizeof(context->last_stage), "%s", safe_stage);
    }

    char status[LAUNCHER_STATUS_MAX] = {0};
    snprintf(status, sizeof(status), "%s... %d%%", safe_stage, percent);
    set_status(status, percent, true);
    ESP_LOGI(TAG, "firmware install progress: %s %d%%", safe_stage, percent);

    if (NULL != context && context->spi_mutex_held) {
        context->spi_mutex_held = false;
        xSemaphoreGive(s_spi2_mutex);
        vTaskDelay(pdMS_TO_TICKS(INSTALL_UI_PROGRESS_RENDER_MS));
        xSemaphoreTake(s_spi2_mutex, portMAX_DELAY);
        context->spi_mutex_held = true;
    }
}

static storage_hal_config_t make_tf_storage_config(void)
{
    const storage_hal_config_t config = {
        .mount_point = TF_MOUNT_POINT,
        .spi_host = SPI2_HOST,
        .cs_gpio_num = BOARD_LAIWFS300_GPIO_TF_SPI_CS,
        .max_freq_khz = TF_SPI_MAX_FREQ_KHZ,
        .max_files = TF_MAX_OPEN_FILES,
        .allocation_unit_size = TF_ALLOCATION_UNIT_SIZE,
        .format_if_mount_failed = false,
    };
    return config;
}

static void scan_task(void *arg)
{
    (void)arg;
    set_status("Mounting TF card...", 0, true);

    bool tf_cd_level = false;
    esp_err_t tf_cd_ret = io_expander_read_pin(BOARD_LAIWFS300_IOEX_TF_CD_PORT,
                                                BOARD_LAIWFS300_IOEX_TF_CD_PIN,
                                                &tf_cd_level);
    if (ESP_OK == tf_cd_ret) {
        ESP_LOGI(TAG, "TF_CD raw level=%d (polarity not verified)", tf_cd_level);
    }

    xSemaphoreTake(s_spi2_mutex, portMAX_DELAY);
    if (!storage_hal_is_mounted()) {
        const storage_hal_config_t storage_config = make_tf_storage_config();
        esp_err_t mount_ret = storage_hal_init(&storage_config);
        if (ESP_OK != mount_ret) {
            xSemaphoreGive(s_spi2_mutex);
            xSemaphoreTake(s_state_mutex, portMAX_DELAY);
            s_state.package_count = 0;
            s_state.rejected_count = 0;
            s_state.selected_index = -1;
            s_state.catalog_generation++;
            snprintf(s_state.status, sizeof(s_state.status), "TF mount failed: %s",
                     esp_err_to_name(mount_ret));
            s_state.progress = 0;
            s_state.busy = false;
            xSemaphoreGive(s_state_mutex);
            vTaskDelete(NULL);
            return;
        }
    }

    set_status("Scanning firmware packages...", 0, true);
    firmware_package_t *packages = calloc(FIRMWARE_PACKAGE_MAX_COUNT,
                                           sizeof(firmware_package_t));
    if (NULL == packages) {
        xSemaphoreGive(s_spi2_mutex);
        set_status("Not enough memory to scan TF card", 0, false);
        vTaskDelete(NULL);
        return;
    }

    size_t package_count = 0;
    size_t rejected_count = 0;
    esp_err_t scan_ret = firmware_package_scan(TF_PACKAGES_ROOT, packages,
                                                FIRMWARE_PACKAGE_MAX_COUNT,
                                                &package_count, &rejected_count);
    xSemaphoreGive(s_spi2_mutex);

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    memset(s_packages, 0, sizeof(s_packages));
    if (ESP_OK == scan_ret && package_count > 0) {
        memcpy(s_packages, packages, package_count * sizeof(packages[0]));
    }
    s_state.package_count = (ESP_OK == scan_ret) ? package_count : 0;
    s_state.rejected_count = rejected_count;
    s_state.selected_index = -1;
    s_state.catalog_generation++;
    s_state.progress = 0;
    s_state.busy = false;
    if (ESP_ERR_NOT_FOUND == scan_ret) {
        snprintf(s_state.status, sizeof(s_state.status), "Missing %s", TF_PACKAGES_ROOT);
    } else if (ESP_OK != scan_ret) {
        snprintf(s_state.status, sizeof(s_state.status), "TF scan failed: %s",
                 esp_err_to_name(scan_ret));
    } else {
        snprintf(s_state.status, sizeof(s_state.status), "%u package(s), %u rejected",
                 (unsigned)package_count, (unsigned)rejected_count);
    }
    xSemaphoreGive(s_state_mutex);

    free(packages);
    vTaskDelete(NULL);
}

static void launch_task(void *arg)
{
    (void)arg;
    firmware_package_t package = {0};

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    int selected_index = s_state.selected_index;
    if (selected_index < 0 || (size_t)selected_index >= s_state.package_count) {
        s_state.busy = false;
        xSemaphoreGive(s_state_mutex);
        vTaskDelete(NULL);
        return;
    }
    package = s_packages[selected_index];
    xSemaphoreGive(s_state_mutex);

    set_status("Preparing selected firmware...", 0, true);
    ESP_LOGI(TAG, "installing %s v%s from %s", package.id, package.version,
             package.app_path);
    vTaskDelay(pdMS_TO_TICKS(INSTALL_UI_INITIAL_RENDER_MS));

    install_progress_context_t progress_context = {
        .last_percent = -1,
    };
    xSemaphoreTake(s_spi2_mutex, portMAX_DELAY);
    progress_context.spi_mutex_held = true;
    esp_err_t ret = firmware_package_install(&package, progress_callback,
                                             &progress_context);
    progress_context.spi_mutex_held = false;
    xSemaphoreGive(s_spi2_mutex);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "firmware install failed: %s", esp_err_to_name(ret));
        char status[LAUNCHER_STATUS_MAX] = {0};
        snprintf(status, sizeof(status), "Launch failed: %s", esp_err_to_name(ret));
        set_status(status, 0, false);
        vTaskDelete(NULL);
        return;
    }

    ESP_LOGI(TAG, "firmware install complete; rebooting into %s", package.id);
    set_status("Install complete. Rebooting...", 100, true);
    vTaskDelay(pdMS_TO_TICKS(INSTALL_RESTART_DELAY_MS));
    esp_restart();
}

static void refresh_button_event_cb(lv_event_t *event)
{
    if (LV_EVENT_CLICKED != lv_event_get_code(event)) {
        return;
    }

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    bool busy = s_state.busy;
    if (!busy) {
        s_state.busy = true;
        s_state.selected_index = -1;
    }
    xSemaphoreGive(s_state_mutex);
    if (busy) {
        return;
    }

    if (pdPASS != xTaskCreate(scan_task, "tf_scan", LAUNCHER_WORKER_STACK, NULL,
                              LAUNCHER_WORKER_PRIORITY, NULL)) {
        set_status("Failed to start TF scan", 0, false);
    }
}

static void run_button_event_cb(lv_event_t *event)
{
    if (LV_EVENT_CLICKED != lv_event_get_code(event)) {
        return;
    }

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    bool can_launch = !s_state.busy && s_state.selected_index >= 0 &&
                      (size_t)s_state.selected_index < s_state.package_count;
    if (can_launch) {
        s_state.busy = true;
    }
    xSemaphoreGive(s_state_mutex);
    if (!can_launch) {
        return;
    }

    if (pdPASS != xTaskCreate(launch_task, "fw_install", LAUNCHER_WORKER_STACK, NULL,
                              LAUNCHER_WORKER_PRIORITY, NULL)) {
        set_status("Failed to start installer", 0, false);
    }
}

static void ui_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    launcher_state_t state = {0};
    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    state = s_state;
    xSemaphoreGive(s_state_mutex);

    if (0 != strcmp(s_rendered_status, state.status)) {
        lv_label_set_text(s_status_label, state.status);
        snprintf(s_rendered_status, sizeof(s_rendered_status), "%s", state.status);
    }
    if (s_rendered_progress != state.progress) {
        lv_bar_set_value(s_progress_bar, state.progress, LV_ANIM_OFF);
        s_rendered_progress = state.progress;
    }
    if (state.busy != s_rendered_busy) {
        s_rendered_busy = state.busy;
        if (state.busy) {
            lv_obj_clear_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_clear_flag(s_package_carousel, LV_OBJ_FLAG_CLICKABLE);
            lv_obj_clear_flag(s_package_carousel, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_add_state(s_refresh_button, LV_STATE_DISABLED);
            lv_obj_add_state(s_run_button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
            lv_obj_add_flag(s_package_carousel, LV_OBJ_FLAG_SCROLLABLE);
            lv_obj_clear_state(s_refresh_button, LV_STATE_DISABLED);
            if (state.selected_index >= 0) {
                lv_obj_clear_state(s_run_button, LV_STATE_DISABLED);
            }
        }
    } else if (state.busy) {
        lv_obj_clear_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_add_state(s_refresh_button, LV_STATE_DISABLED);
        lv_obj_add_state(s_run_button, LV_STATE_DISABLED);
    } else {
        lv_obj_add_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);
        lv_obj_clear_state(s_refresh_button, LV_STATE_DISABLED);
        if (state.selected_index >= 0 && state.package_count > 0) {
            lv_obj_clear_state(s_run_button, LV_STATE_DISABLED);
        } else {
            lv_obj_add_state(s_run_button, LV_STATE_DISABLED);
        }
    }

    if (state.catalog_generation != s_rendered_generation) {
        s_rendered_generation = state.catalog_generation;
        rebuild_package_carousel();
    }
}

static void create_launcher_ui(void)
{
    lv_obj_t *screen = lv_scr_act();
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xf7f8fa), 0);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, LV_SYMBOL_SD_CARD "  Demo Hub");
    lv_obj_set_style_text_font(title, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(title, lv_color_hex(0x172033), 0);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 12, 6);

    s_status_label = lv_label_create(screen);
    lv_obj_set_width(s_status_label, 296);
    lv_obj_set_height(s_status_label, 20);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_status_label, lv_color_hex(0x4b5563), 0);
    lv_obj_set_style_text_align(s_status_label, LV_TEXT_ALIGN_LEFT, 0);
    lv_label_set_long_mode(s_status_label, LV_LABEL_LONG_DOT);
    lv_obj_align(s_status_label, LV_ALIGN_TOP_LEFT, 12, 31);

    s_package_carousel = lv_obj_create(screen);
    lv_obj_set_size(s_package_carousel, LVGL_HOR_RES, CAROUSEL_HEIGHT);
    lv_obj_align(s_package_carousel, LV_ALIGN_TOP_MID, 0, 51);
    lv_obj_set_style_radius(s_package_carousel, 0, 0);
    lv_obj_set_style_bg_opa(s_package_carousel, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_package_carousel, 0, 0);
    lv_obj_set_style_pad_left(s_package_carousel, PACKAGE_CARD_SIDE_PADDING, 0);
    lv_obj_set_style_pad_right(s_package_carousel, PACKAGE_CARD_SIDE_PADDING, 0);
    lv_obj_set_style_pad_top(s_package_carousel, (CAROUSEL_HEIGHT - PACKAGE_CARD_HEIGHT) / 2, 0);
    lv_obj_set_style_pad_bottom(s_package_carousel, (CAROUSEL_HEIGHT - PACKAGE_CARD_HEIGHT) / 2, 0);
    lv_obj_set_style_pad_column(s_package_carousel, PACKAGE_CARD_GAP, 0);
    lv_obj_set_flex_flow(s_package_carousel, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_package_carousel, LV_FLEX_ALIGN_START,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(s_package_carousel, LV_DIR_HOR);
    lv_obj_set_scroll_snap_x(s_package_carousel, LV_SCROLL_SNAP_CENTER);
    lv_obj_add_flag(s_package_carousel, LV_OBJ_FLAG_SCROLL_ONE);
    lv_obj_set_scrollbar_mode(s_package_carousel, LV_SCROLLBAR_MODE_OFF);
    lv_obj_set_style_anim_time(s_package_carousel, 220, 0);
    lv_obj_add_event_cb(s_package_carousel, carousel_scroll_end_event_cb,
                        LV_EVENT_SCROLL_END, NULL);

    s_selected_label = lv_label_create(screen);
    lv_obj_set_width(s_selected_label, 148);
    lv_obj_set_height(s_selected_label, 30);
    lv_label_set_text(s_selected_label, "Select a firmware package");
    lv_label_set_long_mode(s_selected_label, LV_LABEL_LONG_DOT);
    lv_obj_set_style_text_font(s_selected_label, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(s_selected_label, lv_color_hex(0x374151), 0);
    lv_obj_align(s_selected_label, LV_ALIGN_TOP_LEFT, 58, 196);

    s_page_dots_container = lv_obj_create(screen);
    lv_obj_set_size(s_page_dots_container, 120, 12);
    lv_obj_align(s_page_dots_container, LV_ALIGN_TOP_MID, 0, 180);
    lv_obj_set_style_radius(s_page_dots_container, 0, 0);
    lv_obj_set_style_bg_opa(s_page_dots_container, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_page_dots_container, 0, 0);
    lv_obj_set_style_pad_column(s_page_dots_container, 6, 0);
    lv_obj_set_flex_flow(s_page_dots_container, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_page_dots_container, LV_FLEX_ALIGN_CENTER,
                          LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_clear_flag(s_page_dots_container, LV_OBJ_FLAG_SCROLLABLE);

    s_refresh_button = lv_btn_create(screen);
    lv_obj_set_size(s_refresh_button, 36, 34);
    lv_obj_align(s_refresh_button, LV_ALIGN_BOTTOM_LEFT, 10, -10);
    lv_obj_set_style_radius(s_refresh_button, 8, 0);
    lv_obj_set_style_bg_color(s_refresh_button, lv_color_hex(0x2563eb), 0);
    lv_obj_add_event_cb(s_refresh_button, refresh_button_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *refresh_icon = lv_label_create(s_refresh_button);
    lv_label_set_text(refresh_icon, LV_SYMBOL_REFRESH);
    lv_obj_center(refresh_icon);

    s_progress_bar = lv_bar_create(screen);
    lv_obj_set_size(s_progress_bar, 140, 6);
    lv_obj_align(s_progress_bar, LV_ALIGN_BOTTOM_MID, 0, -3);
    lv_bar_set_range(s_progress_bar, 0, 100);
    lv_bar_set_value(s_progress_bar, 0, LV_ANIM_OFF);
    lv_obj_set_style_radius(s_progress_bar, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_radius(s_progress_bar, LV_RADIUS_CIRCLE, LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0xd5dce5), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_progress_bar, lv_color_hex(0x0f766e), LV_PART_INDICATOR);
    lv_obj_add_flag(s_progress_bar, LV_OBJ_FLAG_HIDDEN);

    s_run_button = lv_btn_create(screen);
    lv_obj_set_size(s_run_button, 94, 34);
    lv_obj_align(s_run_button, LV_ALIGN_BOTTOM_RIGHT, -10, -10);
    lv_obj_set_style_radius(s_run_button, 8, 0);
    lv_obj_set_style_bg_color(s_run_button, lv_color_hex(0x0f766e), 0);
    lv_obj_add_state(s_run_button, LV_STATE_DISABLED);
    lv_obj_add_event_cb(s_run_button, run_button_event_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *run_label = lv_label_create(s_run_button);
    lv_label_set_text(run_label, LV_SYMBOL_PLAY " Run");
    lv_obj_center(run_label);

    lv_timer_create(ui_timer_cb, 100, NULL);
}

static void launcher_boot_complete_cb(void *user_ctx)
{
    (void)user_ctx;
    create_launcher_ui();

    xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    s_state.busy = true;
    xSemaphoreGive(s_state_mutex);
    if (pdPASS != xTaskCreate(scan_task, "tf_scan", LAUNCHER_WORKER_STACK, NULL,
                              LAUNCHER_WORKER_PRIORITY, NULL)) {
        set_status("Failed to start TF scan", 0, false);
        return;
    }
    ESP_LOGI(TAG, "launcher UI ready; TF scan started");
}

static void lvgl_task(void *arg)
{
    (void)arg;
    while (true) {
        xSemaphoreTakeRecursive(s_lvgl_mutex, portMAX_DELAY);
        lv_timer_handler();
        xSemaphoreGiveRecursive(s_lvgl_mutex);
        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_DELAY_MS));
    }
}

static esp_err_t initialize_lvgl(bool touch_available)
{
    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    s_state_mutex = xSemaphoreCreateMutex();
    s_spi2_mutex = xSemaphoreCreateMutex();
    ESP_RETURN_ON_FALSE(NULL != s_lvgl_mutex && NULL != s_state_mutex &&
                            NULL != s_spi2_mutex,
                        ESP_ERR_NO_MEM, TAG, "create mutexes");

    s_state.selected_index = -1;
    snprintf(s_state.status, sizeof(s_state.status), "Starting...");

    lv_init();
    size_t buffer_pixels = LVGL_HOR_RES * LVGL_BUFFER_ROWS;
    lv_color_t *buffer_1 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t),
                                            MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    lv_color_t *buffer_2 = heap_caps_malloc(buffer_pixels * sizeof(lv_color_t),
                                            MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == buffer_1 || NULL == buffer_2) {
        heap_caps_free(buffer_1);
        heap_caps_free(buffer_2);
        return ESP_ERR_NO_MEM;
    }

    static lv_disp_draw_buf_t draw_buffer;
    lv_disp_draw_buf_init(&draw_buffer, buffer_1, buffer_2, buffer_pixels);

    static lv_disp_drv_t display_driver;
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = LVGL_HOR_RES;
    display_driver.ver_res = LVGL_VER_RES;
    display_driver.flush_cb = lvgl_flush_cb;
    display_driver.draw_buf = &draw_buffer;
    lv_disp_drv_register(&display_driver);

    if (touch_available) {
        static lv_indev_drv_t input_driver;
        lv_indev_drv_init(&input_driver);
        input_driver.type = LV_INDEV_TYPE_POINTER;
        input_driver.read_cb = lvgl_touch_read_cb;
        lv_indev_drv_register(&input_driver);
    }

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "launcher_lv_tick",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t tick_timer = NULL;
    ESP_RETURN_ON_ERROR(esp_timer_create(&tick_args, &tick_timer), TAG, "create LVGL tick");
    ESP_RETURN_ON_ERROR(esp_timer_start_periodic(tick_timer, LVGL_TICK_MS * 1000U),
                        TAG, "start LVGL tick");

    BaseType_t task_created = xTaskCreatePinnedToCore(lvgl_task, "launcher_lvgl",
                                                      LVGL_TASK_STACK, NULL,
                                                      LVGL_TASK_PRIORITY, NULL,
                                                      LVGL_TASK_CORE);
    return (pdPASS == task_created) ? ESP_OK : ESP_ERR_NO_MEM;
}

void app_main(void)
{
    ESP_LOGI(TAG, "local TF firmware launcher starting");

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = board_laiwfs300_display_init_with_config(LCD_PIXEL_CLOCK_HZ,
                                                    LCD_DRAW_BUFFER_LINES);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = display_hal_set_orientation(true, false, true);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "display orientation failed: %s", esp_err_to_name(ret));
        return;
    }

    bool touch_available = (ESP_OK == board_laiwfs300_touch_init());
    if (!touch_available) {
        ESP_LOGW(TAG, "touch unavailable; package selection is disabled");
    }

    ret = initialize_lvgl(touch_available);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "LVGL init failed: %s", esp_err_to_name(ret));
        return;
    }

    bool skip_boot_assets = false;
    ret = launcher_return_consume_request(&skip_boot_assets);
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "LAUNCHER_RETURN consume failed: %s; using normal boot",
                 esp_err_to_name(ret));
        skip_boot_assets = false;
    }

    xSemaphoreTakeRecursive(s_lvgl_mutex, portMAX_DELAY);
    if (skip_boot_assets) {
        ESP_LOGI(TAG,
                 "LAUNCHER_RETURN active return; skipping boot animation/audio");
        launcher_boot_complete_cb(NULL);
    } else {
        const launcher_boot_config_t boot_config = {
            .storage_config = make_tf_storage_config(),
            .animation_path = TF_BOOT_ANIMATION_PATH,
            .audio_path = TF_BOOT_AUDIO_PATH,
            .lvgl_mutex = s_lvgl_mutex,
            .spi_mutex = s_spi2_mutex,
        };
        ret = launcher_boot_start(&boot_config, launcher_boot_complete_cb, NULL);
        if (ESP_OK != ret) {
            ESP_LOGW(TAG, "boot sequence unavailable: %s", esp_err_to_name(ret));
            launcher_boot_complete_cb(NULL);
        }
    }
    xSemaphoreGiveRecursive(s_lvgl_mutex);

    ESP_LOGI(TAG, "launcher startup initialized%s", touch_available ? " with touch" : "");
}
