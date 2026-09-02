#include "board_laiwfs300.h"
#include "board_pins.h"
#include "display_hal.h"
#include "touch_hal.h"

#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#include "gui_guider.h"
#include "custom.h"

#include <string.h>

static const char *TAG = "lvgl_demo";

#define LCD_H_RES          BOARD_LAIWFS300_LCD_H_RES
#define LCD_V_RES          BOARD_LAIWFS300_LCD_V_RES
#define LVGL_HOR_RES       LCD_V_RES
#define LVGL_VER_RES       LCD_H_RES
#define LVGL_BUFFER_ROWS   BOARD_LAIWFS300_LCD_DRAW_BUF_LINES
#define LVGL_TICK_MS       2
#define LVGL_TASK_DELAY_MS 10
#define LVGL_TASK_STACK    8192
#define LVGL_TASK_PRIORITY 2
#define LVGL_TASK_CORE     1

static SemaphoreHandle_t s_lvgl_mutex = NULL;
lv_ui guider_ui;

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area,
                          lv_color_t *color_map)
{
    uint32_t pixel_count = (area->x2 - area->x1 + 1) * (area->y2 - area->y1 + 1);
    uint16_t *px = (uint16_t *)color_map;
    for (uint32_t i = 0; i < pixel_count; i++) {
        uint16_t p = px[i];
        uint16_t r = (p >> 11) & 0x1F;
        uint16_t g = (p >> 5) & 0x3F;
        uint16_t b = p & 0x1F;
        uint16_t bgr = (b << 11) | (g << 5) | r;
        px[i] = (bgr >> 8) | (bgr << 8);
    }

    int x = area->x1;
    int y = area->y1;
    int w = area->x2 - area->x1 + 1;
    int h = area->y2 - area->y1 + 1;
    display_hal_draw_bitmap_rgb565(x, y, w, h, px);

    lv_disp_flush_ready(drv);
}

static void lvgl_touch_read_cb(lv_indev_drv_t *drv, lv_indev_data_t *data)
{
    (void)drv;
    static lv_coord_t last_x = 0;
    static lv_coord_t last_y = 0;

    touch_panel_point_t point = {0};
    uint8_t count = 0;
    esp_err_t ret = touch_panel_read_point(&point, &count);

    if (ESP_OK == ret && count > 0 && count <= 2) {
        last_x = (lv_coord_t)(LCD_V_RES - 1 - point.y);
        last_y = (lv_coord_t)(point.x);
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

static void lvgl_task(void *arg)
{
    (void)arg;
    uint32_t heartbeat_counter = 0;
    while (true) {
        xSemaphoreTakeRecursive(s_lvgl_mutex, portMAX_DELAY);
        lv_timer_handler();
        xSemaphoreGiveRecursive(s_lvgl_mutex);
        vTaskDelay(pdMS_TO_TICKS(LVGL_TASK_DELAY_MS));

        heartbeat_counter++;
        if (0 == (heartbeat_counter % 1000)) {
            ESP_LOGI(TAG, "heartbeat: %lu s", (unsigned long)(heartbeat_counter / 100));
        }
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "LVGL demo starting (GUI Guider UI, ST7789V3 240x320 + CST836U touch)");

    esp_err_t ret = board_laiwfs300_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "board init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = board_laiwfs300_display_init();
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "display init failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = display_hal_set_orientation(true, false, true);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "display set landscape failed: %s", esp_err_to_name(ret));
        return;
    }

    ret = board_laiwfs300_touch_init();
    if (ESP_OK != ret) {
        ESP_LOGW(TAG, "touch init failed: %s (continuing without touch)", esp_err_to_name(ret));
    }
    bool touch_ok = (ESP_OK == ret);

    s_lvgl_mutex = xSemaphoreCreateRecursiveMutex();
    if (NULL == s_lvgl_mutex) {
        ESP_LOGE(TAG, "failed to create LVGL mutex");
        return;
    }

    lv_init();

    const size_t buf_pixels = LVGL_HOR_RES * LVGL_BUFFER_ROWS;
    lv_color_t *buf1 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t),
                                        MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    lv_color_t *buf2 = heap_caps_malloc(buf_pixels * sizeof(lv_color_t),
                                        MALLOC_CAP_DMA | MALLOC_CAP_8BIT);
    if (NULL == buf1 || NULL == buf2) {
        ESP_LOGE(TAG, "LVGL draw buffer alloc failed (need %u bytes x2)",
                 (unsigned)(buf_pixels * sizeof(lv_color_t)));
        heap_caps_free(buf1);
        heap_caps_free(buf2);
        return;
    }
    ESP_LOGI(TAG, "LVGL draw buffers allocated: %u pixels x2 in DMA memory",
             (unsigned)buf_pixels);

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, buf1, buf2, buf_pixels);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = LVGL_HOR_RES;
    disp_drv.ver_res = LVGL_VER_RES;
    disp_drv.flush_cb = lvgl_flush_cb;
    disp_drv.draw_buf = &draw_buf;
    lv_disp_drv_register(&disp_drv);

    if (touch_ok) {
        static lv_indev_drv_t indev_drv;
        lv_indev_drv_init(&indev_drv);
        indev_drv.type = LV_INDEV_TYPE_POINTER;
        indev_drv.read_cb = lvgl_touch_read_cb;
        lv_indev_drv_register(&indev_drv);
        ESP_LOGI(TAG, "touch input device registered");
    }

    const esp_timer_create_args_t tick_args = {
        .callback = lvgl_tick_cb,
        .arg = NULL,
        .dispatch_method = ESP_TIMER_TASK,
        .name = "lvgl_tick",
        .skip_unhandled_events = true,
    };
    esp_timer_handle_t tick_timer = NULL;
    ret = esp_timer_create(&tick_args, &tick_timer);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "lvgl tick timer create failed: %s", esp_err_to_name(ret));
        return;
    }
    ret = esp_timer_start_periodic(tick_timer, LVGL_TICK_MS * 1000U);
    if (ESP_OK != ret) {
        ESP_LOGE(TAG, "lvgl tick timer start failed: %s", esp_err_to_name(ret));
        return;
    }

    setup_ui(&guider_ui);
    custom_init(&guider_ui);
    ESP_LOGI(TAG, "GUI Guider UI created, starting handler task");

    BaseType_t ok = xTaskCreatePinnedToCore(lvgl_task, "lvgl",
                                            LVGL_TASK_STACK, NULL,
                                            LVGL_TASK_PRIORITY, NULL,
                                            LVGL_TASK_CORE);
    if (pdPASS != ok) {
        ESP_LOGE(TAG, "failed to create LVGL task");
        return;
    }

    ESP_LOGI(TAG, "LVGL demo running%s", touch_ok ? " with touch" : " (no touch)");
}
