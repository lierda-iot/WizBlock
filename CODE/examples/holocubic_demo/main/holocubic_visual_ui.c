#include "holocubic_visual_ui.h"

#include <stdio.h>
#include <string.h>

#define HOLO_COLOR_BLACK 0x0000U
#define HOLO_COLOR_WHITE 0xFFFFU
#define HOLO_COLOR_ICE 0xBFFFU
#define HOLO_COLOR_CYAN 0x4FFFU
#define HOLO_COLOR_DIM 0x39E7U
#define HOLO_COLOR_GREEN 0x47E8U
#define HOLO_COLOR_AMBER 0xFDE0U
#define HOLO_COLOR_RED 0xF9C7U
#define HOLO_FONT_WIDTH 5U
#define HOLO_FONT_HEIGHT 7U
#define HOLO_DIGIT_WIDTH 24
#define HOLO_DIGIT_HEIGHT 42
#define HOLO_DIGIT_THICKNESS 4
#define HOLO_DIGIT_ADVANCE 29
#define HOLO_COLON_ADVANCE 12
#define HOLO_MINUS_ADVANCE 16

enum {
    SEGMENT_A = 1U << 0,
    SEGMENT_B = 1U << 1,
    SEGMENT_C = 1U << 2,
    SEGMENT_D = 1U << 3,
    SEGMENT_E = 1U << 4,
    SEGMENT_F = 1U << 5,
    SEGMENT_G = 1U << 6,
};

static const uint8_t s_font[96U][5U] = {
    ['-' - 32] = {0, 0, 0x1F, 0, 0},
    ['.' - 32] = {0, 0, 0, 0x18, 0x18},
    ['/' - 32] = {1, 2, 4, 8, 16},
    ['0' - 32] = {0x1F, 0x11, 0x11, 0x11, 0x1F},
    ['1' - 32] = {0, 0x12, 0x1F, 0x10, 0},
    ['2' - 32] = {0x12, 0x19, 0x15, 0x13, 0x12},
    ['3' - 32] = {0x11, 0x11, 0x15, 0x15, 0x0A},
    ['4' - 32] = {7, 4, 4, 0x1F, 4},
    ['5' - 32] = {0x17, 0x15, 0x15, 0x15, 9},
    ['6' - 32] = {0x0E, 0x15, 0x15, 0x15, 8},
    ['7' - 32] = {1, 1, 0x1D, 3, 1},
    ['8' - 32] = {0x0A, 0x15, 0x15, 0x15, 0x0A},
    ['9' - 32] = {2, 0x15, 0x15, 0x15, 0x0E},
    ['A' - 32] = {0x1E, 5, 5, 5, 0x1E},
    ['B' - 32] = {0x1F, 0x15, 0x15, 0x15, 0x0A},
    ['C' - 32] = {0x0E, 0x11, 0x11, 0x11, 0x0A},
    ['D' - 32] = {0x1F, 0x11, 0x11, 0x0A, 4},
    ['E' - 32] = {0x1F, 0x15, 0x15, 0x15, 0x11},
    ['F' - 32] = {0x1F, 5, 5, 5, 1},
    ['G' - 32] = {0x0E, 0x11, 0x15, 0x15, 0x1D},
    ['H' - 32] = {0x1F, 4, 4, 4, 0x1F},
    ['I' - 32] = {0x11, 0x11, 0x1F, 0x11, 0x11},
    ['J' - 32] = {8, 0x10, 0x10, 0x10, 0x0F},
    ['K' - 32] = {0x1F, 4, 0x0A, 0x11, 0},
    ['L' - 32] = {0x1F, 0x10, 0x10, 0x10, 0x10},
    ['M' - 32] = {0x1F, 2, 4, 2, 0x1F},
    ['N' - 32] = {0x1F, 2, 4, 8, 0x1F},
    ['O' - 32] = {0x0E, 0x11, 0x11, 0x11, 0x0E},
    ['P' - 32] = {0x1F, 5, 5, 5, 2},
    ['Q' - 32] = {0x0E, 0x11, 0x19, 0x11, 0x1E},
    ['R' - 32] = {0x1F, 5, 0x0D, 0x15, 2},
    ['S' - 32] = {0x12, 0x15, 0x15, 0x15, 9},
    ['T' - 32] = {1, 1, 0x1F, 1, 1},
    ['U' - 32] = {0x0F, 0x10, 0x10, 0x10, 0x0F},
    ['V' - 32] = {7, 8, 0x10, 8, 7},
    ['W' - 32] = {0x1F, 8, 4, 8, 0x1F},
    ['X' - 32] = {0x11, 0x0A, 4, 0x0A, 0x11},
    ['Y' - 32] = {1, 2, 0x1C, 2, 1},
    ['Z' - 32] = {0x19, 0x15, 0x15, 0x13, 0x11},
    [':' - 32] = {0, 0x0A, 0, 0x0A, 0},
    ['%' - 32] = {0x13, 8, 4, 2, 0x19},
    ['+' - 32] = {4, 4, 0x1F, 4, 4},
};

static const uint8_t s_digit_segments[10] = {
    SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_E | SEGMENT_F,
    SEGMENT_B | SEGMENT_C,
    SEGMENT_A | SEGMENT_B | SEGMENT_D | SEGMENT_E | SEGMENT_G,
    SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_G,
    SEGMENT_B | SEGMENT_C | SEGMENT_F | SEGMENT_G,
    SEGMENT_A | SEGMENT_C | SEGMENT_D | SEGMENT_F | SEGMENT_G,
    SEGMENT_A | SEGMENT_C | SEGMENT_D | SEGMENT_E | SEGMENT_F | SEGMENT_G,
    SEGMENT_A | SEGMENT_B | SEGMENT_C,
    SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_E | SEGMENT_F |
        SEGMENT_G,
    SEGMENT_A | SEGMENT_B | SEGMENT_C | SEGMENT_D | SEGMENT_F | SEGMENT_G,
};

static void set_pixel(uint16_t *canvas, int x, int y, uint16_t color)
{
    if (NULL != canvas && 0 <= x && 0 <= y &&
        (int)HOLO_VISUAL_CANVAS_WIDTH > x &&
        (int)HOLO_VISUAL_CANVAS_HEIGHT > y) {
        canvas[(size_t)y * HOLO_VISUAL_CANVAS_WIDTH + (size_t)x] = color;
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
        if (twice_error >= dy) {
            error += dy;
            x0 += sx;
        }
        if (twice_error <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void draw_text(uint16_t *canvas, int x, int y, const char *text,
                      uint16_t color, uint8_t scale)
{
    if (NULL == canvas || NULL == text || 0U == scale) return;
    for (size_t index = 0U; '\0' != text[index]; ++index) {
        const unsigned char character = (unsigned char)text[index];
        const uint8_t *glyph = (32U <= character && 128U > character) ?
                                   s_font[character - 32U] :
                                   s_font[0U];
        for (uint8_t column = 0U; HOLO_FONT_WIDTH > column; ++column) {
            for (uint8_t row = 0U; HOLO_FONT_HEIGHT > row; ++row) {
                if (0U != (glyph[column] & (1U << row))) {
                    draw_rect(canvas, x + (int)column * scale,
                              y + (int)row * scale, scale, scale, color);
                }
            }
        }
        x += (int)(HOLO_FONT_WIDTH + 1U) * scale;
    }
}

static int text_width(const char *text, uint8_t scale)
{
    if (NULL == text || 0U == scale) return 0;
    return (int)strlen(text) * (int)(HOLO_FONT_WIDTH + 1U) * scale;
}

static void draw_text_right(uint16_t *canvas, int right, int y,
                            const char *text, uint16_t color, uint8_t scale)
{
    draw_text(canvas, right - text_width(text, scale), y, text, color, scale);
}

static void draw_text_center(uint16_t *canvas, int center, int y,
                             const char *text, uint16_t color, uint8_t scale)
{
    draw_text(canvas, center - (text_width(text, scale) / 2), y, text, color,
              scale);
}

static void draw_circle(uint16_t *canvas, int center_x, int center_y,
                        int radius, uint16_t color)
{
    int x = radius;
    int y = 0;
    int error = 1 - radius;

    while (x >= y) {
        set_pixel(canvas, center_x + x, center_y + y, color);
        set_pixel(canvas, center_x + y, center_y + x, color);
        set_pixel(canvas, center_x - y, center_y + x, color);
        set_pixel(canvas, center_x - x, center_y + y, color);
        set_pixel(canvas, center_x - x, center_y - y, color);
        set_pixel(canvas, center_x - y, center_y - x, color);
        set_pixel(canvas, center_x + y, center_y - x, color);
        set_pixel(canvas, center_x + x, center_y - y, color);
        y++;
        if (0 > error) {
            error += (2 * y) + 1;
        } else {
            x--;
            error += (2 * (y - x)) + 1;
        }
    }
}

static bool bitmap_is_valid(const holocubic_visual_bitmap_t *bitmap)
{
    return NULL != bitmap && NULL != bitmap->pixels &&
           HOLO_VISUAL_ASSET_WIDTH == bitmap->width &&
           HOLO_VISUAL_ASSET_HEIGHT == bitmap->height &&
           HOLO_VISUAL_ASSET_PIXELS == bitmap->pixel_count;
}

static void draw_bitmap(uint16_t *canvas,
                        const holocubic_visual_bitmap_t *bitmap)
{
    for (uint16_t row = 0U; bitmap->height > row; ++row) {
        memcpy(canvas +
                   ((size_t)(HOLO_VISUAL_ASSET_Y + row) *
                    HOLO_VISUAL_CANVAS_WIDTH) +
                   HOLO_VISUAL_ASSET_X,
               bitmap->pixels + ((size_t)row * bitmap->width),
               (size_t)bitmap->width * sizeof(uint16_t));
    }
}

static void draw_digit(uint16_t *canvas, int x, int y, char character,
                       uint16_t color)
{
    uint8_t segments = 0U;

    if ('0' <= character && '9' >= character) {
        segments = s_digit_segments[(size_t)(character - '0')];
    }
    if (0U != (segments & SEGMENT_A)) {
        draw_rect(canvas, x + HOLO_DIGIT_THICKNESS, y,
                  HOLO_DIGIT_WIDTH - (2 * HOLO_DIGIT_THICKNESS),
                  HOLO_DIGIT_THICKNESS, color);
    }
    if (0U != (segments & SEGMENT_B)) {
        draw_rect(canvas, x + HOLO_DIGIT_WIDTH - HOLO_DIGIT_THICKNESS,
                  y + HOLO_DIGIT_THICKNESS, HOLO_DIGIT_THICKNESS,
                  (HOLO_DIGIT_HEIGHT / 2) - HOLO_DIGIT_THICKNESS, color);
    }
    if (0U != (segments & SEGMENT_C)) {
        draw_rect(canvas, x + HOLO_DIGIT_WIDTH - HOLO_DIGIT_THICKNESS,
                  y + (HOLO_DIGIT_HEIGHT / 2), HOLO_DIGIT_THICKNESS,
                  (HOLO_DIGIT_HEIGHT / 2) - HOLO_DIGIT_THICKNESS, color);
    }
    if (0U != (segments & SEGMENT_D)) {
        draw_rect(canvas, x + HOLO_DIGIT_THICKNESS,
                  y + HOLO_DIGIT_HEIGHT - HOLO_DIGIT_THICKNESS,
                  HOLO_DIGIT_WIDTH - (2 * HOLO_DIGIT_THICKNESS),
                  HOLO_DIGIT_THICKNESS, color);
    }
    if (0U != (segments & SEGMENT_E)) {
        draw_rect(canvas, x, y + (HOLO_DIGIT_HEIGHT / 2),
                  HOLO_DIGIT_THICKNESS,
                  (HOLO_DIGIT_HEIGHT / 2) - HOLO_DIGIT_THICKNESS, color);
    }
    if (0U != (segments & SEGMENT_F)) {
        draw_rect(canvas, x, y + HOLO_DIGIT_THICKNESS,
                  HOLO_DIGIT_THICKNESS,
                  (HOLO_DIGIT_HEIGHT / 2) - HOLO_DIGIT_THICKNESS, color);
    }
    if (0U != (segments & SEGMENT_G)) {
        draw_rect(canvas, x + HOLO_DIGIT_THICKNESS,
                  y + (HOLO_DIGIT_HEIGHT / 2) -
                      (HOLO_DIGIT_THICKNESS / 2),
                  HOLO_DIGIT_WIDTH - (2 * HOLO_DIGIT_THICKNESS),
                  HOLO_DIGIT_THICKNESS, color);
    }
}

static int segment_text_width(const char *text)
{
    int width = 0;

    if (NULL == text) return 0;
    for (size_t index = 0U; '\0' != text[index]; ++index) {
        if (':' == text[index]) width += HOLO_COLON_ADVANCE;
        else if ('-' == text[index]) width += HOLO_MINUS_ADVANCE;
        else width += HOLO_DIGIT_ADVANCE;
    }
    return width;
}

static void draw_segment_text(uint16_t *canvas, int x, int y,
                              const char *text, uint16_t color)
{
    if (NULL == canvas || NULL == text) return;
    for (size_t index = 0U; '\0' != text[index]; ++index) {
        const char character = text[index];
        if (':' == character) {
            draw_rect(canvas, x + 3, y + 11, 4, 4, color);
            draw_rect(canvas, x + 3, y + 27, 4, 4, color);
            x += HOLO_COLON_ADVANCE;
        } else if ('-' == character) {
            draw_rect(canvas, x + 2,
                      y + (HOLO_DIGIT_HEIGHT / 2) -
                          (HOLO_DIGIT_THICKNESS / 2),
                      12, HOLO_DIGIT_THICKNESS, color);
            x += HOLO_MINUS_ADVANCE;
        } else {
            draw_digit(canvas, x, y, character, color);
            x += HOLO_DIGIT_ADVANCE;
        }
    }
}

static void draw_temperature(uint16_t *canvas,
                             const holocubic_weather_t *weather)
{
    char temperature[8] = "--";
    int rounded = 0;

    if (NULL != weather && HOLO_WEATHER_OFFLINE != weather->state) {
        rounded = (0.0f <= weather->temperature_c) ?
                      (int)(weather->temperature_c + 0.5f) :
                      (int)(weather->temperature_c - 0.5f);
        (void)snprintf(temperature, sizeof(temperature), "%d", rounded);
    }
    const int number_width = segment_text_width(temperature);
    const int total_width = number_width + 28;
    const int x = 230 - total_width;
    const int y = 180;
    draw_segment_text(canvas, x, y, temperature, HOLO_COLOR_WHITE);
    draw_circle(canvas, x + number_width + 5, y + 5, 4, HOLO_COLOR_AMBER);
    draw_circle(canvas, x + number_width + 5, y + 5, 3, HOLO_COLOR_AMBER);
    draw_text(canvas, x + number_width + 13, y + 19, "C",
              HOLO_COLOR_AMBER, 2U);
}

holocubic_weather_visual_t holocubic_visual_weather_kind(int16_t weather_code)
{
    if (0 == weather_code) return HOLO_WEATHER_VISUAL_CLEAR;
    if (1 == weather_code || 2 == weather_code) {
        return HOLO_WEATHER_VISUAL_CLOUDY;
    }
    if (3 == weather_code) return HOLO_WEATHER_VISUAL_OVERCAST;
    if (45 == weather_code || 48 == weather_code) {
        return HOLO_WEATHER_VISUAL_FOG;
    }
    if (51 <= weather_code && 67 >= weather_code) {
        return HOLO_WEATHER_VISUAL_RAIN;
    }
    if (71 <= weather_code && 86 >= weather_code) {
        return HOLO_WEATHER_VISUAL_SNOW;
    }
    if (95 <= weather_code && 99 >= weather_code) {
        return HOLO_WEATHER_VISUAL_STORM;
    }
    return HOLO_WEATHER_VISUAL_OFFLINE;
}

bool holocubic_visual_draw_weather(uint16_t *canvas, size_t canvas_pixels,
                                   const holocubic_weather_t *weather,
                                   const holocubic_visual_bitmap_t *bitmap)
{
    char metrics[32] = {0};
    char updated[20] = {0};
    const bool data_valid = NULL != weather &&
                            HOLO_WEATHER_OFFLINE != weather->state;
    const char *condition = data_valid ?
                                holocubic_weather_code_text(
                                    weather->weather_code) :
                                "WEATHER";
    const char *status = data_valid ?
                             (HOLO_WEATHER_STALE == weather->state ?
                                  "STALE" :
                                  "LIVE") :
                             "OFFLINE";
    const uint16_t status_color = data_valid ?
                                      (HOLO_WEATHER_STALE == weather->state ?
                                           HOLO_COLOR_AMBER :
                                           HOLO_COLOR_GREEN) :
                                      HOLO_COLOR_RED;

    if (NULL == canvas || HOLO_VISUAL_CANVAS_PIXELS != canvas_pixels ||
        !bitmap_is_valid(bitmap)) {
        return false;
    }
    memset(canvas, 0, canvas_pixels * sizeof(uint16_t));
    draw_bitmap(canvas, bitmap);
    draw_text(canvas, 14, 8, condition, HOLO_COLOR_ICE, 1U);
    draw_text_right(canvas, 226, 8, status, status_color, 1U);

    if (data_valid) {
        const int high = (int)(weather->high_c +
                               (0.0f <= weather->high_c ? 0.5f : -0.5f));
        const int low = (int)(weather->low_c +
                              (0.0f <= weather->low_c ? 0.5f : -0.5f));
        (void)snprintf(metrics, sizeof(metrics), "H %d  L %d  HUM %u%%",
                       high, low, weather->humidity_percent);
        if ('\0' != weather->observed_at[0] &&
            16U <= strlen(weather->observed_at)) {
            (void)snprintf(updated, sizeof(updated), "UPDATED %.5s",
                           weather->observed_at + 11);
        } else {
            (void)snprintf(updated, sizeof(updated), "LIVE DATA");
        }
    } else {
        (void)snprintf(metrics, sizeof(metrics), "NO LIVE DATA");
        (void)snprintf(updated, sizeof(updated), "NETWORK UNAVAILABLE");
    }
    draw_text_center(canvas, 120, 145, metrics, HOLO_COLOR_CYAN, 1U);
    draw_text_center(canvas, 120, 158, updated, HOLO_COLOR_DIM, 1U);
    draw_line(canvas, 16, 174, 224, 174, HOLO_COLOR_DIM);
    draw_rect(canvas, 16, 173, 40, 2, HOLO_COLOR_CYAN);
    draw_text(canvas, 16, 189, "HANGZHOU", HOLO_COLOR_WHITE, 2U);
    draw_temperature(canvas, data_valid ? weather : NULL);
    return true;
}

static bool clock_text_is_valid(const char *clock_text)
{
    return NULL != clock_text && 8U == strlen(clock_text) &&
           '0' <= clock_text[0] && '9' >= clock_text[0] &&
           '0' <= clock_text[1] && '9' >= clock_text[1] &&
           ':' == clock_text[2] &&
           '0' <= clock_text[3] && '9' >= clock_text[3] &&
           '0' <= clock_text[4] && '9' >= clock_text[4] &&
           ':' == clock_text[5] &&
           '0' <= clock_text[6] && '9' >= clock_text[6] &&
           '0' <= clock_text[7] && '9' >= clock_text[7];
}

bool holocubic_visual_draw_clock(uint16_t *canvas, size_t canvas_pixels,
                                 const char *clock_text,
                                 const char *date_text,
                                 bool time_valid,
                                 const holocubic_visual_bitmap_t *bitmap)
{
    char hour_minute[6] = "--:--";
    char seconds[3] = "--";
    const bool valid = time_valid && clock_text_is_valid(clock_text) &&
                       NULL != date_text && 10U == strlen(date_text);

    if (NULL == canvas || HOLO_VISUAL_CANVAS_PIXELS != canvas_pixels ||
        !bitmap_is_valid(bitmap)) {
        return false;
    }
    memset(canvas, 0, canvas_pixels * sizeof(uint16_t));
    draw_bitmap(canvas, bitmap);
    draw_text(canvas, 14, 8, "BEIJING", HOLO_COLOR_ICE, 1U);
    draw_text_right(canvas, 226, 8, valid ? "SYNCED" : "UNSYNCED",
                    valid ? HOLO_COLOR_GREEN : HOLO_COLOR_AMBER, 1U);
    draw_line(canvas, 28, 137, 212, 137, HOLO_COLOR_DIM);
    draw_rect(canvas, 94, 136, 52, 2, HOLO_COLOR_CYAN);

    if (valid) {
        memcpy(hour_minute, clock_text, 5U);
        hour_minute[5] = '\0';
        memcpy(seconds, clock_text + 6, 2U);
        seconds[2] = '\0';
    }
    draw_segment_text(canvas,
                      120 - (segment_text_width(hour_minute) / 2) - 7,
                      144, hour_minute, HOLO_COLOR_WHITE);
    draw_text(canvas, 196, 163, seconds, HOLO_COLOR_CYAN, 2U);
    draw_text_center(canvas, 120, 196,
                     valid ? date_text : "TIME UNSYNCED",
                     valid ? HOLO_COLOR_ICE : HOLO_COLOR_AMBER,
                     valid ? 2U : 1U);
    if (valid) {
        draw_text_center(canvas, 120, 212, "CST  UTC+8", HOLO_COLOR_DIM,
                         1U);
    }
    return true;
}
