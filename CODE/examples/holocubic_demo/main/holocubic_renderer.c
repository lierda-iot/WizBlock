#include "holocubic_renderer.h"

#include "board_laiwfs300.h"
#include "display_hal.h"
#include "holocubic_display_domain.h"
#include "holocubic_frame_format.h"
#include "holocubic_visual_assets.h"
#include "holocubic_visual_ui.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include <string.h>

static const char *TAG = "holocubic_render";

#define BLACK 0x0000U
#define WHITE 0xFFFFU
#define CYAN 0x07FFU
#define YELLOW 0xFFE0U
#define GREEN 0x07E0U
#define HOLO_FONT_WIDTH 5U
#define HOLO_FONT_HEIGHT 7U

static const uint8_t s_font[96U][5U] = {
    [32] = {0, 0, 0, 0, 0}, [45] = {0, 0, 0x1F, 0, 0},
    [46] = {0, 0, 0, 0x18, 0x18}, [47] = {1, 2, 4, 8, 16},
    ['0' - 32] = {0x1F, 0x11, 0x11, 0x11, 0x1F},
    ['1' - 32] = {0, 0x12, 0x1F, 0x10, 0},
    ['2' - 32] = {0x12, 0x19, 0x15, 0x13, 0x12},
    ['3' - 32] = {0x11, 0x11, 0x15, 0x15, 0x0A},
    ['4' - 32] = {7, 4, 4, 0x1F, 4}, ['5' - 32] = {0x17, 0x15, 0x15, 0x15, 9},
    ['6' - 32] = {0x0E, 0x15, 0x15, 0x15, 8}, ['7' - 32] = {1, 1, 0x1D, 3, 1},
    ['8' - 32] = {0x0A, 0x15, 0x15, 0x15, 0x0A}, ['9' - 32] = {2, 0x15, 0x15, 0x15, 0x0E},
    ['A' - 32] = {0x1E, 5, 5, 5, 0x1E}, ['B' - 32] = {0x1F, 0x15, 0x15, 0x15, 0x0A},
    ['C' - 32] = {0x0E, 0x11, 0x11, 0x11, 0x0A}, ['D' - 32] = {0x1F, 0x11, 0x11, 0x0A, 4},
    ['E' - 32] = {0x1F, 0x15, 0x15, 0x15, 0x11}, ['F' - 32] = {0x1F, 5, 5, 5, 1},
    ['G' - 32] = {0x0E, 0x11, 0x15, 0x15, 0x1D}, ['H' - 32] = {0x1F, 4, 4, 4, 0x1F},
    ['I' - 32] = {0x11, 0x11, 0x1F, 0x11, 0x11}, ['J' - 32] = {8, 0x10, 0x10, 0x10, 0x0F},
    ['K' - 32] = {0x1F, 4, 0x0A, 0x11, 0}, ['L' - 32] = {0x1F, 0x10, 0x10, 0x10, 0x10},
    ['M' - 32] = {0x1F, 2, 4, 2, 0x1F}, ['N' - 32] = {0x1F, 2, 4, 8, 0x1F},
    ['O' - 32] = {0x0E, 0x11, 0x11, 0x11, 0x0E}, ['P' - 32] = {0x1F, 5, 5, 5, 2},
    ['Q' - 32] = {0x0E, 0x11, 0x19, 0x11, 0x1E}, ['R' - 32] = {0x1F, 5, 0x0D, 0x15, 2},
    ['S' - 32] = {0x12, 0x15, 0x15, 0x15, 9}, ['T' - 32] = {1, 1, 0x1F, 1, 1},
    ['U' - 32] = {0x0F, 0x10, 0x10, 0x10, 0x0F}, ['V' - 32] = {7, 8, 0x10, 8, 7},
    ['W' - 32] = {0x1F, 8, 4, 8, 0x1F}, ['X' - 32] = {0x11, 0x0A, 4, 0x0A, 0x11},
    ['Y' - 32] = {1, 2, 0x1C, 2, 1}, ['Z' - 32] = {0x19, 0x15, 0x15, 0x13, 0x11},
    [':' - 32] = {0, 0x0A, 0, 0x0A, 0}, ['/' - 32] = {1, 2, 4, 8, 16},
    ['%' - 32] = {0x13, 8, 4, 2, 0x19}, ['+' - 32] = {4, 4, 0x1F, 4, 4},
};

static void set_pixel(uint16_t *canvas, int x, int y, uint16_t color)
{
    if (NULL != canvas && x >= 0 && y >= 0 && x < (int)HOLO_LOGICAL_WIDTH &&
        y < (int)HOLO_LOGICAL_HEIGHT) {
        canvas[(size_t)y * HOLO_LOGICAL_WIDTH + (size_t)x] = color;
    }
}

static void draw_rect(uint16_t *canvas, int x, int y, int width, int height,
                      uint16_t color)
{
    for (int row = 0; row < height; ++row) {
        for (int column = 0; column < width; ++column) {
            set_pixel(canvas, x + column, y + row, color);
        }
    }
}

static void draw_line(uint16_t *canvas, int x0, int y0, int x1, int y1,
                      uint16_t color)
{
    int dx = (x1 > x0) ? x1 - x0 : x0 - x1;
    int sx = (x0 < x1) ? 1 : -1;
    int dy = (y1 > y0) ? y0 - y1 : y1 - y0;
    int sy = (y0 < y1) ? 1 : -1;
    int error = dx + dy;

    for (;;) {
        set_pixel(canvas, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        int twice_error = 2 * error;
        if (twice_error >= dy) { error += dy; x0 += sx; }
        if (twice_error <= dx) { error += dx; y0 += sy; }
    }
}

static void draw_text(uint16_t *canvas, int x, int y, const char *text,
                      uint16_t color, uint8_t scale)
{
    if (NULL == canvas || NULL == text || 0U == scale) return;
    for (size_t index = 0U; text[index] != '\0'; ++index) {
        unsigned char character = (unsigned char)text[index];
        const uint8_t *glyph = (character >= 32U && character < 128U) ?
                               s_font[character - 32U] : s_font[0];
        for (uint8_t column = 0U; column < HOLO_FONT_WIDTH; ++column) {
            for (uint8_t row = 0U; row < HOLO_FONT_HEIGHT; ++row) {
                if (0U != (glyph[column] & (1U << row))) {
                    draw_rect(canvas, x + (int)column * scale,
                              y + (int)row * scale, scale, scale, color);
                }
            }
        }
        x += (int)(HOLO_FONT_WIDTH + 1U) * scale;
    }
}

static void draw_animation(uint16_t *canvas, uint32_t frame_index)
{
    int offset = (int)(frame_index % 20U);
    int left = 54 + offset;
    int top = 52;
    int right = 184 + offset;
    int bottom = 182;

    draw_line(canvas, left, top, right, top, CYAN);
    draw_line(canvas, right, top, right, bottom, CYAN);
    draw_line(canvas, right, bottom, left, bottom, CYAN);
    draw_line(canvas, left, bottom, left, top, CYAN);
    draw_line(canvas, left + 26, top - 22, right + 26, top - 22, GREEN);
    draw_line(canvas, right + 26, top - 22, right + 26, bottom - 22, GREEN);
    draw_line(canvas, right + 26, bottom - 22, left + 26, bottom - 22, GREEN);
    draw_line(canvas, left + 26, bottom - 22, left + 26, top - 22, GREEN);
    draw_line(canvas, left, top, left + 26, top - 22, YELLOW);
    draw_line(canvas, right, top, right + 26, top - 22, YELLOW);
    draw_line(canvas, right, bottom, right + 26, bottom - 22, YELLOW);
    draw_line(canvas, left, bottom, left + 26, bottom - 22, YELLOW);
    draw_rect(canvas, 112 + (offset / 2), 108, 14, 14, WHITE);
    draw_text(canvas, 72, 198, "HOLOCUBIC", WHITE, 2U);
}

static void draw_weather(uint16_t *canvas, const holocubic_weather_t *weather)
{
    holocubic_visual_bitmap_t bitmap = {0};
    const holocubic_weather_visual_t kind =
        (NULL == weather || HOLO_WEATHER_OFFLINE == weather->state) ?
            HOLO_WEATHER_VISUAL_OFFLINE :
            holocubic_visual_weather_kind(weather->weather_code);

    if (!holocubic_visual_assets_weather(kind, &bitmap) ||
        !holocubic_visual_draw_weather(canvas, HOLO_VISUAL_CANVAS_PIXELS,
                                       weather, &bitmap)) {
        draw_text(canvas, 72, 104, "WEATHER OFFLINE", YELLOW, 2U);
    }
}

static void draw_clock(uint16_t *canvas, const char *clock_text,
                       const char *date_text, bool time_valid)
{
    holocubic_visual_bitmap_t bitmap = {0};

    if (!holocubic_visual_assets_clock(&bitmap) ||
        !holocubic_visual_draw_clock(canvas, HOLO_VISUAL_CANVAS_PIXELS,
                                     clock_text, date_text, time_valid,
                                     &bitmap)) {
        draw_text(canvas, 38, 104, time_valid ? clock_text : "--:--:--",
                  WHITE, 3U);
    }
}

esp_err_t holocubic_renderer_init(holocubic_renderer_t *renderer)
{
    holocubic_render_policy_t policy = holocubic_render_policy_default();
    size_t scale_buffer_bytes = 0U;

    if (NULL == renderer) return ESP_ERR_INVALID_ARG;
    memset(renderer, 0, sizeof(*renderer));
    renderer->content_rect = holocubic_layout(HOLO_RENDER_PHYSICAL_WIDTH,
                                              HOLO_RENDER_PHYSICAL_HEIGHT);
    if (!holocubic_render_buffer_bytes(&policy, renderer->content_rect.width,
                                       sizeof(uint16_t),
                                       &scale_buffer_bytes)) {
        return ESP_ERR_INVALID_SIZE;
    }
    renderer->canvas = heap_caps_calloc(HOLO_RENDER_FRAME_PIXELS,
                                        sizeof(uint16_t),
                                        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == renderer->canvas) {
        renderer->canvas = heap_caps_calloc(HOLO_RENDER_FRAME_PIXELS,
                                            sizeof(uint16_t), MALLOC_CAP_8BIT);
    }
    if (NULL == renderer->canvas) return ESP_ERR_NO_MEM;
    uint32_t scale_buffer_caps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
    if (policy.source_dma_required) {
        scale_buffer_caps |= MALLOC_CAP_DMA;
    }
    renderer->scale_buffer = heap_caps_calloc(1U, scale_buffer_bytes,
                                              scale_buffer_caps);
    if (NULL == renderer->scale_buffer) {
        heap_caps_free(renderer->canvas);
        renderer->canvas = NULL;
        return ESP_ERR_NO_MEM;
    }
    renderer->chunk_lines = policy.chunk_lines;
    renderer->horizontal_mirror = policy.horizontal_mirror;
    renderer->rotate_180 = policy.rotate_180;
    renderer->ready = true;
    ESP_LOGI(TAG, "ready logical=%ux%u physical=%ux%u rect=(%u,%u,%u,%u) mirror=%s rotate=%s",
             HOLO_LOGICAL_WIDTH, HOLO_LOGICAL_HEIGHT,
             HOLO_RENDER_PHYSICAL_WIDTH, HOLO_RENDER_PHYSICAL_HEIGHT,
             renderer->content_rect.x, renderer->content_rect.y,
             renderer->content_rect.width, renderer->content_rect.height,
             renderer->horizontal_mirror ? "horizontal" : "off",
             renderer->rotate_180 ? "180" : "off");
    return ESP_OK;
}

void holocubic_renderer_deinit(holocubic_renderer_t *renderer)
{
    if (NULL != renderer) {
        heap_caps_free(renderer->canvas);
        heap_caps_free(renderer->scale_buffer);
        renderer->canvas = NULL;
        renderer->scale_buffer = NULL;
        renderer->ready = false;
    }
}

void holocubic_renderer_draw(holocubic_renderer_t *renderer,
                             holocubic_page_t page,
                             const holocubic_weather_t *weather,
                             const char *clock_text,
                             const char *date_text,
                             bool time_valid,
                             const uint16_t *animation_frame,
                             const holocubic_spectrum_snapshot_t *spectrum,
                             holocubic_spectrum_mode_t spectrum_mode,
                             uint32_t now_ms,
                             uint32_t frame_index)
{
    if (NULL == renderer || !renderer->ready || NULL == renderer->canvas) return;
    const bool external_frame_ready =
        HOLO_PAGE_ANIMATION == page && NULL != animation_frame &&
        holocubic_frame_scale_to_canvas(animation_frame, HOLO_FRAME_PIXELS,
                                        renderer->canvas,
                                        HOLO_RENDER_FRAME_PIXELS);
    if (!external_frame_ready) {
        memset(renderer->canvas, 0,
               HOLO_RENDER_FRAME_PIXELS * sizeof(uint16_t));
        if (HOLO_PAGE_ANIMATION == page) draw_animation(renderer->canvas, frame_index);
        else if (HOLO_PAGE_WEATHER == page) draw_weather(renderer->canvas, weather);
        else if (HOLO_PAGE_CLOCK == page) {
            draw_clock(renderer->canvas, clock_text, date_text, time_valid);
        } else {
            holocubic_spectrum_draw(renderer->canvas, spectrum, spectrum_mode,
                                    now_ms);
        }
    }
    for (uint8_t index = 0U; index < HOLO_PAGE_COUNT; ++index) {
        draw_rect(renderer->canvas, 105 + index * 12,
                  224, (index == (uint8_t)page) ? 6 : 3, 3,
                  index == (uint8_t)page ? WHITE : 0x7BEFU);
    }
    renderer->frame_index = frame_index;
}

esp_err_t holocubic_renderer_present(holocubic_renderer_t *renderer)
{
    if (NULL == renderer || !renderer->ready || NULL == renderer->canvas ||
        NULL == renderer->scale_buffer || 0U == renderer->chunk_lines) {
        return ESP_ERR_INVALID_STATE;
    }
    esp_err_t result = holocubic_display_domain_lock(1000);
    if (ESP_OK != result) return result;
    for (uint16_t row = 0U; row < renderer->content_rect.height;
         row += renderer->chunk_lines) {
        const uint16_t remaining = renderer->content_rect.height - row;
        const uint16_t lines = (remaining < renderer->chunk_lines) ?
                               remaining : renderer->chunk_lines;
        const bool direct_1_to_1 =
            renderer->content_rect.width == HOLO_LOGICAL_WIDTH &&
            renderer->content_rect.height == HOLO_LOGICAL_HEIGHT;
        for (uint16_t output_y = 0U; output_y < lines; ++output_y) {
            const uint16_t source_y = direct_1_to_1 ?
                (renderer->rotate_180 ?
                 (uint16_t)(HOLO_LOGICAL_HEIGHT - 1U - row - output_y) :
                 (uint16_t)(row + output_y)) :
                holocubic_scale_coordinate(
                    holocubic_render_sample_y(
                        renderer->rotate_180, (uint16_t)(row + output_y),
                        renderer->content_rect.height),
                    renderer->content_rect.height, HOLO_LOGICAL_HEIGHT);
            const uint16_t *source_row = renderer->canvas +
                (size_t)source_y * HOLO_LOGICAL_WIDTH;
            for (uint16_t output_x = 0U;
                 output_x < renderer->content_rect.width; ++output_x) {
                const uint16_t source_x = direct_1_to_1 ?
                    ((renderer->horizontal_mirror == renderer->rotate_180) ?
                     output_x :
                     (uint16_t)(HOLO_LOGICAL_WIDTH - 1U - output_x)) :
                    holocubic_scale_coordinate(
                        holocubic_render_sample_x(
                            renderer->horizontal_mirror, renderer->rotate_180,
                            output_x, renderer->content_rect.width),
                        renderer->content_rect.width, HOLO_LOGICAL_WIDTH);
                renderer->scale_buffer[(size_t)output_y *
                                       renderer->content_rect.width + output_x] =
                    holocubic_rgb565_to_display(source_row[source_x]);
            }
        }
        result = display_hal_draw_bitmap_rgb565(
            renderer->content_rect.x, renderer->content_rect.y + row,
            renderer->content_rect.width, lines, renderer->scale_buffer);
        if (ESP_OK != result) {
            holocubic_display_domain_unlock();
            return result;
        }
        result = display_hal_wait_pending(1000);
        if (ESP_OK != result) {
            holocubic_display_domain_unlock();
            return result;
        }
    }
    holocubic_display_domain_unlock();
    return ESP_OK;
}

esp_err_t holocubic_renderer_clear_background(void)
{
    return display_hal_fill_rgb565(BLACK);
}
