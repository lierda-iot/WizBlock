#include "xiaozhi_ui.h"
#include "board_laiwfs300.h"
#include "display_hal.h"
#include "touch_hal.h"

#include "esp_log.h"
#include "lvgl.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "xiaozhi_ui";

static lv_obj_t *s_status_label;
static lv_color_t *s_draw_buf;

#define DISP_HOR_RES  320
#define DISP_VER_RES  240

static void lvgl_flush_cb(lv_disp_drv_t *drv, const lv_area_t *area, lv_color_t *color_p)
{
    int x1 = area->x1;
    int y1 = area->y1;
    int x2 = area->x2;
    int y2 = area->y2;
    int w = x2 - x1 + 1;
    int h = y2 - y1 + 1;
    display_hal_draw_bitmap_rgb565(x1, y1, w, h, (const uint16_t *)color_p);
    lv_disp_flush_ready(drv);
}

static void lvgl_tick_task(void *arg)
{
    (void)arg;
    while (true) {
        lv_timer_handler();
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

esp_err_t xiaozhi_ui_init(void)
{
    lv_init();

    s_draw_buf = heap_caps_malloc(DISP_HOR_RES * 40 * sizeof(lv_color_t), MALLOC_CAP_SPIRAM);
    if (NULL == s_draw_buf) {
        ESP_LOGE(TAG, "draw buf alloc failed");
        return ESP_ERR_NO_MEM;
    }

    static lv_disp_draw_buf_t draw_buf;
    lv_disp_draw_buf_init(&draw_buf, s_draw_buf, NULL, DISP_HOR_RES * 40);

    static lv_disp_drv_t disp_drv;
    lv_disp_drv_init(&disp_drv);
    disp_drv.hor_res = DISP_HOR_RES;
    disp_drv.ver_res = DISP_VER_RES;
    disp_drv.draw_buf = &draw_buf;
    disp_drv.flush_cb = lvgl_flush_cb;
    lv_disp_drv_register(&disp_drv);

    lv_obj_t *scr = lv_scr_act();
    lv_obj_set_style_bg_color(scr, lv_color_black(), 0);

    s_status_label = lv_label_create(scr);
    lv_label_set_text(s_status_label, "Initializing...");
    lv_obj_set_style_text_color(s_status_label, lv_color_white(), 0);
    lv_obj_set_style_text_font(s_status_label, &lv_font_montserrat_14, 0);
    lv_obj_align(s_status_label, LV_ALIGN_CENTER, 0, 0);

    xTaskCreate(lvgl_tick_task, "lvgl_tick", 4096, NULL, 2, NULL);

    ESP_LOGI(TAG, "UI initialized");
    return ESP_OK;
}

void xiaozhi_ui_set_state(xiaozhi_ui_state_t state)
{
    if (NULL == s_status_label) {
        return;
    }

    switch (state) {
    case XIAOZHI_UI_STATE_LTE_CONNECTING:
        lv_label_set_text(s_status_label, "LTE Connecting...");
        lv_obj_set_style_text_color(s_status_label, lv_color_make(255, 200, 0), 0);
        break;
    case XIAOZHI_UI_STATE_NETWORK_OK:
        lv_label_set_text(s_status_label, "Network OK");
        lv_obj_set_style_text_color(s_status_label, lv_color_make(0, 200, 0), 0);
        break;
    case XIAOZHI_UI_STATE_AGENT_READY:
        lv_label_set_text(s_status_label, "XiaoZhi Ready");
        lv_obj_set_style_text_color(s_status_label, lv_color_make(0, 200, 0), 0);
        break;
    case XIAOZHI_UI_STATE_NETWORK_LOST:
        lv_label_set_text(s_status_label, "Network Lost");
        lv_obj_set_style_text_color(s_status_label, lv_color_make(255, 50, 50), 0);
        break;
    case XIAOZHI_UI_STATE_LISTENING:
        lv_label_set_text(s_status_label, "Listening...");
        lv_obj_set_style_text_color(s_status_label, lv_color_make(50, 150, 255), 0);
        break;
    case XIAOZHI_UI_STATE_SPEAKING:
        lv_label_set_text(s_status_label, "Speaking...");
        lv_obj_set_style_text_color(s_status_label, lv_color_make(50, 150, 255), 0);
        break;
    }
}
