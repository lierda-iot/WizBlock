#include "board_laiwfs300.h"

#include "board_pins.h"
#include "display_hal.h"
#include "io_expander.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"

static const char *TAG = "board_display";

static esp_err_t set_lcd_reset_level(void *ctx, bool level)
{
    (void)ctx;
    return io_expander_write_pin(BOARD_LAIWFS300_IOEX_LCD_RST_PORT,
                                 BOARD_LAIWFS300_IOEX_LCD_RST_PIN,
                                 level);
}

esp_err_t board_laiwfs300_display_init_with_config(uint32_t pixel_clock_hz, int draw_buffer_lines)
{
    if (ESP_OK == display_hal_probe()) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(io_expander_write_pin(BOARD_LAIWFS300_IOEX_LCD_RST_PORT,
                                              BOARD_LAIWFS300_IOEX_LCD_RST_PIN,
                                              !BOARD_LAIWFS300_LCD_RESET_ASSERT_LEVEL),
                        TAG,
                        "preset LCD reset release");
    ESP_RETURN_ON_ERROR(io_expander_set_pin_direction(BOARD_LAIWFS300_IOEX_LCD_RST_PORT,
                                                      BOARD_LAIWFS300_IOEX_LCD_RST_PIN,
                                                      true),
                        TAG,
                        "LCD reset output");

    const display_hal_config_t config = {
        .spi_host = SPI2_HOST,
        .sclk_gpio_num = BOARD_LAIWFS300_GPIO_LCD_SPI_SCK,
        .mosi_gpio_num = BOARD_LAIWFS300_GPIO_LCD_SPI_MOSI,
        .miso_gpio_num = GPIO_NUM_NC,
        .cs_gpio_num = BOARD_LAIWFS300_GPIO_LCD_SPI_CS,
        .dc_gpio_num = BOARD_LAIWFS300_GPIO_LCD_DC,
        .backlight_gpio_num = BOARD_LAIWFS300_GPIO_LCD_BACKLIGHT,
        .backlight_on_level = BOARD_LAIWFS300_LCD_BACKLIGHT_ON_LEVEL,
        .set_reset_level = set_lcd_reset_level,
        .reset_ctx = NULL,
        .reset_assert_level = BOARD_LAIWFS300_LCD_RESET_ASSERT_LEVEL,
        .h_res = BOARD_LAIWFS300_LCD_H_RES,
        .v_res = BOARD_LAIWFS300_LCD_V_RES,
        .pixel_clock_hz = (int)pixel_clock_hz,
        .draw_buffer_lines = draw_buffer_lines,
        .color_invert = BOARD_LAIWFS300_LCD_COLOR_INVERT,
        .mirror_x = BOARD_LAIWFS300_LCD_MIRROR_X,
        .mirror_y = BOARD_LAIWFS300_LCD_MIRROR_Y,
        .swap_xy = BOARD_LAIWFS300_LCD_SWAP_XY,
    };

    ESP_RETURN_ON_ERROR(display_hal_init(&config), TAG, "display init");
    ESP_LOGI(TAG, "display mapped: MOSI=GPIO%d SCK=GPIO%d CS=GPIO%d DC=GPIO%d BL=GPIO%d LCD_RST=IOEX P%d_%d pclk=%lu",
             BOARD_LAIWFS300_GPIO_LCD_SPI_MOSI,
             BOARD_LAIWFS300_GPIO_LCD_SPI_SCK,
             BOARD_LAIWFS300_GPIO_LCD_SPI_CS,
             BOARD_LAIWFS300_GPIO_LCD_DC,
             BOARD_LAIWFS300_GPIO_LCD_BACKLIGHT,
             BOARD_LAIWFS300_IOEX_LCD_RST_PORT,
             BOARD_LAIWFS300_IOEX_LCD_RST_PIN,
             (unsigned long)pixel_clock_hz);
    return ESP_OK;
}

esp_err_t board_laiwfs300_display_init_with_pclk(uint32_t pixel_clock_hz)
{
    return board_laiwfs300_display_init_with_config(pixel_clock_hz,
                                                    BOARD_LAIWFS300_LCD_DRAW_BUF_LINES);
}

esp_err_t board_laiwfs300_display_init(void)
{
    return board_laiwfs300_display_init_with_config(BOARD_LAIWFS300_LCD_PIXEL_CLOCK_HZ,
                                                    BOARD_LAIWFS300_LCD_DRAW_BUF_LINES);
}

esp_err_t board_laiwfs300_display_fill_rgb565(uint16_t color)
{
    ESP_RETURN_ON_ERROR(board_laiwfs300_display_init(), TAG, "display init");
    return display_hal_fill_rgb565(color);
}

esp_err_t board_laiwfs300_display_draw_bitmap_rgb565(int x, int y, int w, int h, const uint16_t *data)
{
    ESP_RETURN_ON_ERROR(board_laiwfs300_display_init(), TAG, "display init");
    return display_hal_draw_bitmap_rgb565(x, y, w, h, data);
}

esp_err_t board_laiwfs300_display_deinit(void)
{
    return display_hal_deinit();
}
