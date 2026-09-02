#include "display_hal.h"

#include "driver/gpio.h"
#include "esp_check.h"
#include "esp_heap_caps.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_st7789.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

#include <string.h>

static const char *TAG = "display_hal";

#define DISPLAY_HAL_INTERNAL_WAIT_TIMEOUT_MS 1000

static display_hal_config_t s_config;
static esp_lcd_panel_io_handle_t s_lcd_io;
static esp_lcd_panel_handle_t s_lcd_panel;
static bool s_initialized;
static SemaphoreHandle_t s_flush_done_sem;

static bool on_lcd_color_trans_done(esp_lcd_panel_io_handle_t panel_io,
                                    esp_lcd_panel_io_event_data_t *edata,
                                    void *user_ctx)
{
    (void)panel_io;
    (void)edata;
    (void)user_ctx;

    if (s_flush_done_sem == NULL) {
        return false;
    }

    BaseType_t woken = pdFALSE;
    xSemaphoreGiveFromISR(s_flush_done_sem, &woken);
    return (woken == pdTRUE);
}

static esp_err_t validate_config(const display_hal_config_t *config)
{
    ESP_RETURN_ON_FALSE(NULL != config, ESP_ERR_INVALID_ARG, TAG, "missing config");
    ESP_RETURN_ON_FALSE(config->sclk_gpio_num >= 0 && config->mosi_gpio_num >= 0 &&
                            config->cs_gpio_num >= 0 && config->dc_gpio_num >= 0,
                        ESP_ERR_INVALID_ARG, TAG, "invalid SPI pins");
    ESP_RETURN_ON_FALSE(config->h_res > 0 && config->v_res > 0 &&
                            config->pixel_clock_hz > 0 && config->draw_buffer_lines > 0,
                        ESP_ERR_INVALID_ARG, TAG, "invalid geometry");
    return ESP_OK;
}

static esp_err_t set_backlight(bool on)
{
    if (s_config.backlight_gpio_num < 0) {
        return ESP_OK;
    }

    int level = on ? s_config.backlight_on_level : !s_config.backlight_on_level;
    return gpio_set_level((gpio_num_t)s_config.backlight_gpio_num, level);
}

static esp_err_t reset_panel_by_board_control(void)
{
    if (NULL == s_config.set_reset_level) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(s_config.set_reset_level(s_config.reset_ctx, s_config.reset_assert_level),
                        TAG, "assert LCD reset");
    vTaskDelay(pdMS_TO_TICKS(20));
    ESP_RETURN_ON_ERROR(s_config.set_reset_level(s_config.reset_ctx, !s_config.reset_assert_level),
                        TAG, "release LCD reset");
    vTaskDelay(pdMS_TO_TICKS(120));
    return ESP_OK;
}

esp_err_t display_hal_init(const display_hal_config_t *config)
{
    if (s_initialized) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(validate_config(config), TAG, "config");
    s_config = *config;

    ESP_LOGI(TAG, "init ST7789V3 LCD %dx%d SPI host=%d", s_config.h_res, s_config.v_res, s_config.spi_host);

    if (s_config.backlight_gpio_num >= 0) {
        const gpio_config_t backlight_gpio = {
            .pin_bit_mask = 1ULL << s_config.backlight_gpio_num,
            .mode = GPIO_MODE_OUTPUT,
            .pull_up_en = GPIO_PULLUP_DISABLE,
            .pull_down_en = GPIO_PULLDOWN_DISABLE,
            .intr_type = GPIO_INTR_DISABLE,
        };
        ESP_RETURN_ON_ERROR(gpio_config(&backlight_gpio), TAG, "backlight gpio");
        ESP_RETURN_ON_ERROR(set_backlight(false), TAG, "backlight off");
    }

    ESP_RETURN_ON_ERROR(reset_panel_by_board_control(), TAG, "board reset");

    const spi_bus_config_t bus_config = {
        .sclk_io_num = s_config.sclk_gpio_num,
        .mosi_io_num = s_config.mosi_gpio_num,
        .miso_io_num = s_config.miso_gpio_num,
        .quadwp_io_num = GPIO_NUM_NC,
        .quadhd_io_num = GPIO_NUM_NC,
        .max_transfer_sz = s_config.h_res * s_config.draw_buffer_lines * sizeof(uint16_t),
    };
    esp_err_t ret = spi_bus_initialize(s_config.spi_host, &bus_config, SPI_DMA_CH_AUTO);
    if (ret != ESP_ERR_INVALID_STATE) {
        ESP_RETURN_ON_ERROR(ret, TAG, "SPI bus init");
    }

    const esp_lcd_panel_io_spi_config_t io_config = {
        .dc_gpio_num = s_config.dc_gpio_num,
        .cs_gpio_num = s_config.cs_gpio_num,
        .pclk_hz = s_config.pixel_clock_hz,
        .on_color_trans_done = on_lcd_color_trans_done,
        .user_ctx = NULL,
        .lcd_cmd_bits = 8,
        .lcd_param_bits = 8,
        .spi_mode = 0,
        .trans_queue_depth = 10,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_io_spi((esp_lcd_spi_bus_handle_t)s_config.spi_host,
                                                 &io_config,
                                                 &s_lcd_io),
                        TAG, "panel IO");

    const esp_lcd_panel_dev_config_t panel_config = {
        .reset_gpio_num = GPIO_NUM_NC,
        .rgb_ele_order = LCD_RGB_ELEMENT_ORDER_BGR,
        .bits_per_pixel = 16,
    };
    ESP_RETURN_ON_ERROR(esp_lcd_new_panel_st7789(s_lcd_io, &panel_config, &s_lcd_panel),
                        TAG, "ST7789 panel");

    s_flush_done_sem = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(NULL != s_flush_done_sem, ESP_ERR_NO_MEM, TAG, "flush semaphore");

    ESP_RETURN_ON_ERROR(esp_lcd_panel_reset(s_lcd_panel), TAG, "panel reset");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_init(s_lcd_panel), TAG, "panel init");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_invert_color(s_lcd_panel, s_config.color_invert),
                        TAG, "invert color");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_lcd_panel, s_config.swap_xy), TAG, "swap xy");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_lcd_panel, s_config.mirror_x, s_config.mirror_y),
                        TAG, "mirror");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_disp_on_off(s_lcd_panel, true), TAG, "display on");
    ESP_RETURN_ON_ERROR(set_backlight(true), TAG, "backlight on");

    s_initialized = true;
    ESP_LOGI(TAG, "ST7789V3 LCD initialized");
    return ESP_OK;
}

esp_err_t display_hal_deinit(void)
{
    if (!s_initialized) {
        return ESP_OK;
    }

    if (NULL != s_lcd_panel) {
        esp_lcd_panel_disp_on_off(s_lcd_panel, false);
        esp_lcd_panel_del(s_lcd_panel);
        s_lcd_panel = NULL;
    }
    if (NULL != s_lcd_io) {
        esp_lcd_panel_io_del(s_lcd_io);
        s_lcd_io = NULL;
    }
    if (NULL != s_flush_done_sem) {
        vSemaphoreDelete(s_flush_done_sem);
        s_flush_done_sem = NULL;
    }
    if (s_config.backlight_gpio_num >= 0) {
        gpio_set_level((gpio_num_t)s_config.backlight_gpio_num, !s_config.backlight_on_level);
    }

    s_initialized = false;
    ESP_LOGI(TAG, "display deinitialized");
    return ESP_OK;
}

esp_err_t display_hal_probe(void)
{
    return s_initialized ? ESP_OK : ESP_ERR_INVALID_STATE;
}

esp_err_t display_hal_fill_rgb565(uint16_t color)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "display not initialized");

    const int chunk_lines = s_config.draw_buffer_lines;
    const int chunk_pixels = s_config.h_res * chunk_lines;
    uint16_t *buf = heap_caps_malloc(chunk_pixels * sizeof(uint16_t), MALLOC_CAP_DMA);
    ESP_RETURN_ON_FALSE(NULL != buf, ESP_ERR_NO_MEM, TAG, "fill buffer");

    for (int i = 0; i < chunk_pixels; ++i) {
        buf[i] = color;
    }

    for (int y = 0; y < s_config.v_res; y += chunk_lines) {
        int y_end = y + chunk_lines;
        if (y_end > s_config.v_res) {
            y_end = s_config.v_res;
        }

        esp_err_t ret = esp_lcd_panel_draw_bitmap(s_lcd_panel, 0, y, s_config.h_res, y_end, buf);
        if (ESP_OK != ret) {
            heap_caps_free(buf);
            return ret;
        }
        ret = display_hal_wait_pending(DISPLAY_HAL_INTERNAL_WAIT_TIMEOUT_MS);
        if (ESP_OK != ret) {
            heap_caps_free(buf);
            return ret;
        }
    }

    heap_caps_free(buf);
    return ESP_OK;
}

esp_err_t display_hal_draw_bitmap_rgb565(int x, int y, int w, int h, const uint16_t *data)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "display not initialized");
    ESP_RETURN_ON_FALSE(NULL != data && w > 0 && h > 0, ESP_ERR_INVALID_ARG, TAG, "invalid bitmap args");
    ESP_RETURN_ON_FALSE(x >= 0 && y >= 0 && (x + w) <= s_config.h_res && (y + h) <= s_config.v_res,
                        ESP_ERR_INVALID_ARG, TAG, "bitmap out of bounds");

    return esp_lcd_panel_draw_bitmap(s_lcd_panel, x, y, x + w, y + h, data);
}

esp_err_t display_hal_wait_pending(int timeout_ms)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "display not initialized");
    ESP_RETURN_ON_FALSE(timeout_ms >= 0, ESP_ERR_INVALID_ARG, TAG, "invalid timeout");
    ESP_RETURN_ON_FALSE(NULL != s_flush_done_sem, ESP_ERR_INVALID_STATE, TAG, "flush semaphore not initialized");

    if (pdTRUE != xSemaphoreTake(s_flush_done_sem, pdMS_TO_TICKS(timeout_ms))) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}

esp_err_t display_hal_set_orientation(bool swap_xy, bool mirror_x, bool mirror_y)
{
    ESP_RETURN_ON_FALSE(s_initialized, ESP_ERR_INVALID_STATE, TAG, "display not initialized");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_swap_xy(s_lcd_panel, swap_xy), TAG, "swap xy");
    ESP_RETURN_ON_ERROR(esp_lcd_panel_mirror(s_lcd_panel, mirror_x, mirror_y), TAG, "mirror");
    if (swap_xy != s_config.swap_xy) {
        int tmp = s_config.h_res;
        s_config.h_res = s_config.v_res;
        s_config.v_res = tmp;
        s_config.swap_xy = swap_xy;
    }
    s_config.mirror_x = mirror_x;
    s_config.mirror_y = mirror_y;
    return ESP_OK;
}
