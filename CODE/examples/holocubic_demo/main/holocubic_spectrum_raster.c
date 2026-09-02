#include "holocubic_spectrum_raster.h"

#include "holocubic_spectrum_visual_math.h"

#include <math.h>
#include <stddef.h>
#include <string.h>

#define RASTER_PI 3.14159265358979323846f
#define RASTER_CENTER_X 120
#define RASTER_BLACK 0U
#define RASTER_WHITE_R 238U
#define RASTER_WHITE_G 247U
#define RASTER_WHITE_B 255U
#define RASTER_TRAIL_COUNT 6U
#define RASTER_LEVEL_MIN_DBFS (-72.0f)
#define RASTER_LEVEL_MAX_DBFS 0.0f

typedef struct {
    uint8_t red;
    uint8_t green;
    uint8_t blue;
} raster_rgb_t;

static const raster_rgb_t s_band_colors[] = {
    {32U, 192U, 255U},  {32U, 204U, 248U},  {35U, 215U, 234U},
    {40U, 224U, 211U},  {48U, 230U, 191U},  {59U, 232U, 173U},
    {78U, 229U, 156U},  {101U, 225U, 143U}, {130U, 222U, 130U},
    {160U, 220U, 118U}, {190U, 218U, 107U}, {216U, 215U, 96U},
    {237U, 205U, 83U},  {247U, 190U, 74U},  {255U, 177U, 70U},
    {255U, 159U, 74U},  {255U, 139U, 86U},  {255U, 119U, 105U},
    {250U, 102U, 132U}, {237U, 94U, 157U},  {217U, 94U, 184U},
    {190U, 104U, 208U}, {155U, 122U, 225U}, {117U, 143U, 236U},
};

static float clamp_unit(float value)
{
    if (!isfinite(value) || 0.0f > value) return 0.0f;
    return 1.0f < value ? 1.0f : value;
}

static float clamp_angle(float value)
{
    if (!isfinite(value)) return 0.0f;
    if (-90.0f > value) return -90.0f;
    return 90.0f < value ? 90.0f : value;
}

static uint16_t rgb565(raster_rgb_t color)
{
    return (uint16_t)(((uint16_t)(color.red & 0xF8U) << 8U) |
                      ((uint16_t)(color.green & 0xFCU) << 3U) |
                      ((uint16_t)color.blue >> 3U));
}

static raster_rgb_t mix_rgb(raster_rgb_t first,
                            raster_rgb_t second,
                            uint8_t second_weight)
{
    const uint16_t weight = second_weight;
    const uint16_t inverse = 255U - weight;
    return (raster_rgb_t){
        .red = (uint8_t)((first.red * inverse + second.red * weight) / 255U),
        .green = (uint8_t)((first.green * inverse + second.green * weight) /
                           255U),
        .blue = (uint8_t)((first.blue * inverse + second.blue * weight) /
                          255U),
    };
}

static raster_rgb_t band_color(size_t index, uint8_t brightness)
{
    const raster_rgb_t base = s_band_colors[index % AUDIO_SPECTRUM_BAND_COUNT];
    const raster_rgb_t black = {0U, 0U, 0U};
    return mix_rgb(black, base, brightness);
}

static raster_rgb_t heat_color(float level)
{
    static const raster_rgb_t stops[] = {
        {4U, 8U, 22U},    {0U, 66U, 150U},  {0U, 178U, 226U},
        {53U, 224U, 166U}, {240U, 218U, 70U}, {255U, 94U, 72U},
        {255U, 230U, 190U},
    };
    const float position = clamp_unit(level) * 6.0f;
    size_t index = (size_t)position;
    if (6U <= index) index = 5U;
    const uint8_t fraction = (uint8_t)((position - (float)index) * 255.0f);
    return mix_rgb(stops[index], stops[index + 1U], fraction);
}

static raster_rgb_t channel_color(size_t index, bool warm)
{
    const raster_rgb_t tint = warm ? (raster_rgb_t){255U, 132U, 84U}
                                   : (raster_rgb_t){42U, 211U, 255U};
    return mix_rgb(s_band_colors[index % AUDIO_SPECTRUM_BAND_COUNT], tint,
                   96U);
}

static void put_pixel(uint16_t *canvas, int x, int y, uint16_t color)
{
    if (NULL == canvas || 0 > x || 0 > y ||
        (int)HOLO_SPECTRUM_CANVAS_WIDTH <= x ||
        (int)HOLO_SPECTRUM_CANVAS_HEIGHT <= y) {
        return;
    }
    canvas[(size_t)y * HOLO_SPECTRUM_CANVAS_WIDTH + (size_t)x] = color;
}

static void fill_rect(uint16_t *canvas, int x, int y, int width, int height,
                      uint16_t color)
{
    if (NULL == canvas || 0 >= width || 0 >= height ||
        x >= (int)HOLO_SPECTRUM_CANVAS_WIDTH ||
        y >= (int)HOLO_SPECTRUM_CANVAS_HEIGHT ||
        x + width <= 0 || y + height <= 0) {
        return;
    }
    const int first_x = 0 > x ? 0 : x;
    const int first_y = 0 > y ? 0 : y;
    const int last_x = (int)HOLO_SPECTRUM_CANVAS_WIDTH < x + width ?
                       (int)HOLO_SPECTRUM_CANVAS_WIDTH : x + width;
    const int last_y = (int)HOLO_SPECTRUM_CANVAS_HEIGHT < y + height ?
                       (int)HOLO_SPECTRUM_CANVAS_HEIGHT : y + height;
    for (int row = first_y; row < last_y; ++row) {
        uint16_t *destination = &canvas[(size_t)row *
                                        HOLO_SPECTRUM_CANVAS_WIDTH +
                                        (size_t)first_x];
        for (int column = first_x; column < last_x; ++column) {
            *destination++ = color;
        }
    }
}

static void fill_round_rect(uint16_t *canvas, int x, int y, int width,
                            int height, int radius, uint16_t color)
{
    if (0 >= radius || radius * 2 >= width || radius * 2 >= height) {
        fill_rect(canvas, x, y, width, height, color);
        return;
    }
    for (int row = 0; row < height; ++row) {
        int inset = 0;
        if (row < radius) {
            const int distance = radius - row;
            inset = radius - (int)sqrtf((float)(radius * radius -
                                                distance * distance));
        } else if (row >= height - radius) {
            const int distance = row - (height - radius - 1);
            inset = radius - (int)sqrtf((float)(radius * radius -
                                                distance * distance));
        }
        fill_rect(canvas, x + inset, y + row, width - inset * 2, 1, color);
    }
}

static void fill_circle(uint16_t *canvas, int center_x, int center_y,
                        int radius, uint16_t color)
{
    if (0 >= radius) return;
    for (int row = -radius; row <= radius; ++row) {
        const int extent = (int)sqrtf((float)(radius * radius - row * row));
        fill_rect(canvas, center_x - extent, center_y + row,
                  extent * 2 + 1, 1, color);
    }
}

static void draw_circle(uint16_t *canvas, int center_x, int center_y,
                        int radius, uint16_t color)
{
    if (0 >= radius) return;
    int x = radius;
    int y = 0;
    int error = 1 - radius;
    while (x >= y) {
        put_pixel(canvas, center_x + x, center_y + y, color);
        put_pixel(canvas, center_x + y, center_y + x, color);
        put_pixel(canvas, center_x - y, center_y + x, color);
        put_pixel(canvas, center_x - x, center_y + y, color);
        put_pixel(canvas, center_x - x, center_y - y, color);
        put_pixel(canvas, center_x - y, center_y - x, color);
        put_pixel(canvas, center_x + y, center_y - x, color);
        put_pixel(canvas, center_x + x, center_y - y, color);
        y++;
        if (0 > error) {
            error += 2 * y + 1;
        } else {
            x--;
            error += 2 * (y - x + 1);
        }
    }
}

static void draw_line(uint16_t *canvas, int x0, int y0, int x1, int y1,
                      uint16_t color)
{
    int dx = x1 - x0;
    const int sx = 0 > dx ? -1 : 1;
    int dy = y1 - y0;
    const int sy = 0 > dy ? -1 : 1;
    int error = 0;

    if (0 > dx) dx = -dx;
    if (0 > dy) dy = -dy;
    dy = -dy;
    error = dx + dy;
    for (;;) {
        put_pixel(canvas, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        const int doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void draw_thick_line(uint16_t *canvas, int x0, int y0, int x1, int y1,
                            int thickness, uint16_t color)
{
    int dx = x1 - x0;
    const int sx = 0 > dx ? -1 : 1;
    int dy = y1 - y0;
    const int sy = 0 > dy ? -1 : 1;
    int error = 0;

    if (0 > dx) dx = -dx;
    if (0 > dy) dy = -dy;
    dy = -dy;
    error = dx + dy;
    const int radius = thickness <= 1 ? 0 : thickness / 2;
    for (;;) {
        fill_rect(canvas, x0 - radius, y0 - radius, thickness, thickness,
                  color);
        if (x0 == x1 && y0 == y1) break;
        const int doubled = error * 2;
        if (doubled >= dy) {
            error += dy;
            x0 += sx;
        }
        if (doubled <= dx) {
            error += dx;
            y0 += sy;
        }
    }
}

static void draw_arc(uint16_t *canvas, int center_x, int center_y, int radius,
                     float start_deg, float end_deg, int thickness,
                     uint16_t color)
{
    float previous_angle = start_deg;
    int previous_x = center_x + (int)lroundf(
        sinf(previous_angle * RASTER_PI / 180.0f) * (float)radius);
    int previous_y = center_y - (int)lroundf(
        cosf(previous_angle * RASTER_PI / 180.0f) * (float)radius);
    for (float angle = start_deg + 4.0f; angle <= end_deg + 0.1f;
         angle += 4.0f) {
        const int point_x = center_x + (int)lroundf(
            sinf(angle * RASTER_PI / 180.0f) * (float)radius);
        const int point_y = center_y - (int)lroundf(
            cosf(angle * RASTER_PI / 180.0f) * (float)radius);
        draw_thick_line(canvas, previous_x, previous_y, point_x, point_y,
                        thickness, color);
        previous_angle = angle;
        previous_x = point_x;
        previous_y = point_y;
    }
    (void)previous_angle;
}

static void draw_dot(uint16_t *canvas, int center_x, int center_y, int radius,
                     raster_rgb_t color)
{
    fill_circle(canvas, center_x, center_y, radius, rgb565(color));
}

static void draw_vertical_bar(uint16_t *canvas, int x, int baseline,
                              int maximum_height, int width, float level,
                              size_t band, bool down)
{
    const int height = (int)lroundf(clamp_unit(level) *
                                    (float)maximum_height);
    const int top = down ? baseline : baseline - maximum_height;
    fill_round_rect(canvas, x, top, width, maximum_height, 3,
                    rgb565((raster_rgb_t){7U, 20U, 29U}));
    if (0 >= height) return;
    const raster_rgb_t bright = channel_color(band, down);
    const raster_rgb_t dim = mix_rgb((raster_rgb_t){0U, 0U, 0U}, bright, 58U);
    const int fill_y = down ? baseline : baseline - height;
    for (int row = 0; row < height; ++row) {
        const float position = down ? (float)row / (float)maximum_height :
                                      1.0f - (float)row /
                                      (float)maximum_height;
        const uint8_t amount = (uint8_t)(54.0f + position * 201.0f);
        fill_rect(canvas, x, fill_y + (down ? row : row), width, 1,
                  rgb565(mix_rgb(dim, bright, amount)));
    }
    fill_round_rect(canvas, x, down ? fill_y + height - 3 : fill_y,
                    width, 3, 2, rgb565(bright));
}

static void draw_horizontal_meter(uint16_t *canvas, int x, int y, int width,
                                  int height, float level, raster_rgb_t color)
{
    const int fill_width = (int)lroundf(clamp_unit(level) * (float)width);
    fill_round_rect(canvas, x, y, width, height, height / 2,
                    rgb565((raster_rgb_t){7U, 20U, 29U}));
    if (0 >= fill_width) return;
    const raster_rgb_t dim = mix_rgb((raster_rgb_t){0U, 0U, 0U}, color, 62U);
    for (int column = 0; column < fill_width; ++column) {
        const uint8_t amount = (uint8_t)(54.0f +
            201.0f * (float)column / (float)(width - 1));
        fill_rect(canvas, x + column, y, 1, height,
                  rgb565(mix_rgb(dim, color, amount)));
    }
    fill_round_rect(canvas, x + fill_width - 4, y, 4, height, height / 2,
                    rgb565(color));
}

static void draw_side_meter(uint16_t *canvas, int x, float level,
                            raster_rgb_t color)
{
    const int top = 50;
    const int height = 124;
    const int fill_height = (int)lroundf(clamp_unit(level) * (float)height);
    fill_round_rect(canvas, x, top, 4, height, 2,
                    rgb565((raster_rgb_t){7U, 20U, 29U}));
    if (0 < fill_height) {
        fill_round_rect(canvas, x, top + height - fill_height, 4, fill_height,
                        2, rgb565(color));
    }
}

static void update_history(const holocubic_spectrum_snapshot_t *snapshot,
                           holocubic_spectrum_raster_state_t *state)
{
    if (NULL == snapshot || NULL == state ||
        snapshot->revision == state->waterfall_revision) {
        return;
    }
    state->waterfall_revision = snapshot->revision;
    memcpy(state->waterfall[state->waterfall_head], snapshot->combined_levels,
           sizeof(state->waterfall[state->waterfall_head]));
    state->waterfall_head = (uint8_t)((state->waterfall_head + 1U) %
                                      HOLO_SPECTRUM_WATERFALL_ROWS);
    if (state->waterfall_count < HOLO_SPECTRUM_WATERFALL_ROWS) {
        state->waterfall_count++;
    }
    if (snapshot->doa_active) {
        const uint8_t limit = state->doa_trail_count < RASTER_TRAIL_COUNT ?
                              state->doa_trail_count : RASTER_TRAIL_COUNT - 1U;
        for (uint8_t index = limit; index > 0U; --index) {
            state->doa_trail[index] = state->doa_trail[index - 1U];
        }
        state->doa_trail[0] = clamp_angle(snapshot->relative_angle_deg);
        if (state->doa_trail_count < RASTER_TRAIL_COUNT) {
            state->doa_trail_count++;
        }
        state->doa_revision = snapshot->revision;
    }
}

static raster_rgb_t direction_color(float angle)
{
    if (-18.0f < angle && 18.0f > angle) {
        return (raster_rgb_t){71U, 226U, 178U};
    }
    return angle < 0.0f ? (raster_rgb_t){54U, 199U, 255U}
                        : (raster_rgb_t){255U, 148U, 76U};
}

static void draw_radar(uint16_t *canvas,
                       const holocubic_spectrum_snapshot_t *snapshot,
                       const holocubic_spectrum_raster_state_t *state)
{
    const int center_y = 111;
    draw_side_meter(canvas, 9, snapshot->mic1_level,
                    (raster_rgb_t){38U, 205U, 255U});
    draw_side_meter(canvas, 227, snapshot->mic2_level,
                    (raster_rgb_t){255U, 157U, 84U});
    draw_circle(canvas, RASTER_CENTER_X, center_y, 96,
                rgb565((raster_rgb_t){8U, 25U, 35U}));
    draw_circle(canvas, RASTER_CENTER_X, center_y, 64,
                rgb565((raster_rgb_t){10U, 36U, 47U}));
    draw_circle(canvas, RASTER_CENTER_X, center_y, 36,
                rgb565((raster_rgb_t){15U, 49U, 58U}));
    for (size_t index = 0U; index < AUDIO_SPECTRUM_BAND_COUNT; ++index) {
        const float angle = (float)index * 360.0f /
                            (float)AUDIO_SPECTRUM_BAND_COUNT - 90.0f;
        const float radians = angle * RASTER_PI / 180.0f;
        const float level_radius = 39.0f +
                                   clamp_unit(snapshot->combined_levels[index]) *
                                   42.0f;
        const int base_x = RASTER_CENTER_X +
                           (int)lroundf(cosf(radians) * 38.0f);
        const int base_y = center_y + (int)lroundf(sinf(radians) * 38.0f);
        const int limit_x = RASTER_CENTER_X +
                            (int)lroundf(cosf(radians) * 81.0f);
        const int limit_y = center_y + (int)lroundf(sinf(radians) * 81.0f);
        const int value_x = RASTER_CENTER_X +
                            (int)lroundf(cosf(radians) * level_radius);
        const int value_y = center_y +
                            (int)lroundf(sinf(radians) * level_radius);
        const float peak_radius = 39.0f +
                                  clamp_unit(snapshot->combined_peaks[index]) *
                                  42.0f;
        const int peak_x = RASTER_CENTER_X +
                           (int)lroundf(cosf(radians) * peak_radius);
        const int peak_y = center_y + (int)lroundf(sinf(radians) * peak_radius);
        draw_thick_line(canvas, base_x, base_y, limit_x, limit_y, 4,
                        rgb565((raster_rgb_t){8U, 27U, 36U}));
        draw_thick_line(canvas, base_x, base_y, value_x, value_y, 5,
                        rgb565(band_color(index, 218U)));
        draw_dot(canvas, peak_x, peak_y, 2, (raster_rgb_t){235U, 249U, 255U});
    }
    draw_arc(canvas, RASTER_CENTER_X, center_y, 97, -90.0f, 90.0f, 2,
             rgb565((raster_rgb_t){24U, 57U, 68U}));
    for (int angle = -90; angle <= 90; angle += 15) {
        const float radians = (float)angle * RASTER_PI / 180.0f;
        const int outer_x = RASTER_CENTER_X +
                            (int)lroundf(sinf(radians) * 99.0f);
        const int outer_y = center_y -
                            (int)lroundf(cosf(radians) * 99.0f);
        const int inner_x = RASTER_CENTER_X +
                            (int)lroundf(sinf(radians) * 93.0f);
        const int inner_y = center_y -
                            (int)lroundf(cosf(radians) * 93.0f);
        draw_line(canvas, inner_x, inner_y, outer_x, outer_y,
                  rgb565((raster_rgb_t){39U, 77U, 84U}));
    }
    if (NULL != state && 0U < state->doa_trail_count) {
        for (uint8_t index = state->doa_trail_count; index > 0U; --index) {
            const uint8_t trail_index = index - 1U;
            const float radians = state->doa_trail[trail_index] * RASTER_PI /
                                  180.0f;
            const int point_x = RASTER_CENTER_X +
                                (int)lroundf(sinf(radians) * 97.0f);
            const int point_y = center_y -
                                (int)lroundf(cosf(radians) * 97.0f);
            const uint8_t brightness = (uint8_t)(42U +
                (state->doa_trail_count - trail_index) * 28U);
            draw_dot(canvas, point_x, point_y,
                     trail_index == 0U ? 4 : 2,
                     mix_rgb((raster_rgb_t){0U, 0U, 0U},
                             direction_color(state->doa_trail[trail_index]),
                             brightness));
        }
    }
    if (snapshot->doa_active) {
        const float radians = clamp_angle(snapshot->relative_angle_deg) *
                              RASTER_PI / 180.0f;
        const int point_x = RASTER_CENTER_X +
                            (int)lroundf(sinf(radians) * 97.0f);
        const int point_y = center_y -
                            (int)lroundf(cosf(radians) * 97.0f);
        draw_dot(canvas, point_x, point_y, 6,
                 mix_rgb((raster_rgb_t){0U, 0U, 0U},
                         direction_color(snapshot->relative_angle_deg), 96U));
        draw_dot(canvas, point_x, point_y, 3,
                 direction_color(snapshot->relative_angle_deg));
    }
    draw_dot(canvas, RASTER_CENTER_X, center_y, 14,
             (raster_rgb_t){4U, 16U, 25U});
    draw_dot(canvas, RASTER_CENTER_X, center_y, 8,
             (raster_rgb_t){23U, 99U, 116U});
    draw_dot(canvas, RASTER_CENTER_X, center_y, 3,
             (raster_rgb_t){RASTER_WHITE_R, RASTER_WHITE_G, RASTER_WHITE_B});
}

static void draw_mirror(uint16_t *canvas,
                        const holocubic_spectrum_snapshot_t *snapshot)
{
    const int center_y = 111;
    draw_line(canvas, 11, center_y, 228, center_y,
              rgb565((raster_rgb_t){28U, 64U, 73U}));
    draw_line(canvas, 11, 21, 228, 21,
              rgb565((raster_rgb_t){8U, 26U, 35U}));
    draw_line(canvas, 11, 201, 228, 201,
              rgb565((raster_rgb_t){8U, 26U, 35U}));
    for (size_t index = 0U; index < AUDIO_SPECTRUM_BAND_COUNT; ++index) {
        const int x = 12 + (int)index * 9;
        const float level = clamp_unit(snapshot->combined_levels[index]);
        const float peak = clamp_unit(snapshot->combined_peaks[index]);
        const int peak_offset = (int)lroundf(peak * 86.0f);
        fill_round_rect(canvas, x, center_y - 86, 7, 173, 3,
                        rgb565((raster_rgb_t){7U, 20U, 29U}));
        draw_vertical_bar(canvas, x, center_y, 86, 7, level, index, false);
        draw_vertical_bar(canvas, x, center_y + 1, 86, 7, level, index, true);
        draw_line(canvas, x, center_y - peak_offset, x + 6,
                  center_y - peak_offset,
                  rgb565((raster_rgb_t){235U, 249U, 255U}));
        draw_line(canvas, x, center_y + peak_offset, x + 6,
                  center_y + peak_offset,
                  rgb565((raster_rgb_t){82U, 109U, 118U}));
    }
    if (snapshot->doa_active) {
        const int marker_x = RASTER_CENTER_X + (int)lroundf(
            clamp_angle(snapshot->relative_angle_deg) * 1.04f);
        draw_thick_line(canvas, marker_x, 18, marker_x, 204, 2,
                        rgb565(direction_color(snapshot->relative_angle_deg)));
        draw_dot(canvas, marker_x, center_y, 5,
                 direction_color(snapshot->relative_angle_deg));
    }
}

static void draw_waterfall(uint16_t *canvas,
                           const holocubic_spectrum_snapshot_t *snapshot,
                           holocubic_spectrum_raster_state_t *state)
{
    update_history(snapshot, state);
    const uint8_t visible_count = NULL == state ? 0U : state->waterfall_count;
    const uint8_t oldest = NULL == state || visible_count <
                           HOLO_SPECTRUM_WATERFALL_ROWS ? 0U :
                           state->waterfall_head;
    for (uint8_t row = 0U; row < visible_count; ++row) {
        const uint8_t history_row = (uint8_t)((oldest + row) %
                                              HOLO_SPECTRUM_WATERFALL_ROWS);
        for (size_t band = 0U; band < AUDIO_SPECTRUM_BAND_COUNT; ++band) {
            const int x = 12 + (int)band * 9;
            const int y = 20 + (int)row * 10;
            const float level = clamp_unit(state->waterfall[history_row][band]);
            fill_round_rect(canvas, x, y, 8, 8, 2,
                            rgb565(heat_color(level)));
        }
    }
    for (size_t band = 0U; band <= AUDIO_SPECTRUM_BAND_COUNT; band += 4U) {
        const int x = 10 + (int)band * 9;
        draw_line(canvas, x, 17, x, 201,
                  rgb565((raster_rgb_t){7U, 19U, 28U}));
    }
    for (int row = 0; row <= 18; row += 3) {
        const int y = 18 + row * 10;
        draw_line(canvas, 10, y, 229, y,
                  rgb565((raster_rgb_t){7U, 19U, 28U}));
    }
}

static int clamp_position(int value, int minimum, int maximum)
{
    if (value < minimum) return minimum;
    return value > maximum ? maximum : value;
}

static void draw_energy_ball(uint16_t *canvas, int center_x, int center_y,
                             int radius, raster_rgb_t color)
{
    draw_dot(canvas, center_x, center_y, radius + 10,
             mix_rgb((raster_rgb_t){0U, 0U, 0U}, color, 22U));
    draw_dot(canvas, center_x, center_y, radius + 6,
             mix_rgb((raster_rgb_t){0U, 0U, 0U}, color, 54U));
    draw_dot(canvas, center_x, center_y, radius,
             mix_rgb((raster_rgb_t){0U, 0U, 0U}, color, 210U));
    draw_dot(canvas, center_x - radius / 3, center_y - radius / 3,
             radius > 6 ? radius - 6 : radius,
             mix_rgb(color, (raster_rgb_t){RASTER_WHITE_R, RASTER_WHITE_G,
                                           RASTER_WHITE_B}, 78U));
    draw_dot(canvas, center_x - radius / 2, center_y - radius / 2, 2,
             (raster_rgb_t){RASTER_WHITE_R, RASTER_WHITE_G, RASTER_WHITE_B});
}

static void draw_metaballs(uint16_t *canvas,
                           const holocubic_spectrum_snapshot_t *snapshot,
                           uint32_t now_ms,
                           holocubic_spectrum_raster_state_t *state)
{
    static const int base_x[HOLO_SPECTRUM_METABALL_COUNT] = {72, 120, 169};
    static const int base_y[HOLO_SPECTRUM_METABALL_COUNT] = {116, 103, 128};
    static const float amplitude_x[HOLO_SPECTRUM_METABALL_COUNT] =
        {24.0f, 34.0f, 28.0f};
    static const float amplitude_y[HOLO_SPECTRUM_METABALL_COUNT] =
        {30.0f, 27.0f, 26.0f};
    static const float speed_x[HOLO_SPECTRUM_METABALL_COUNT] =
        {0.77f, 0.59f, 0.43f};
    static const float speed_y[HOLO_SPECTRUM_METABALL_COUNT] =
        {0.61f, 0.83f, 0.67f};
    static const float phase[HOLO_SPECTRUM_METABALL_COUNT] =
        {0.0f, 1.8f, 3.9f};
    static const int base_radius[HOLO_SPECTRUM_METABALL_COUNT] = {12, 13, 11};
    static const int radius_range[HOLO_SPECTRUM_METABALL_COUNT] = {26, 30, 24};
    static const raster_rgb_t colors[HOLO_SPECTRUM_METABALL_COUNT] = {
        {34U, 196U, 255U}, {68U, 226U, 161U}, {255U, 184U, 72U},
    };
    const float levels[HOLO_SPECTRUM_METABALL_COUNT] = {
        audio_spectrum_average_band_levels(
            snapshot->combined_levels, AUDIO_SPECTRUM_BAND_COUNT, 0U, 8U),
        audio_spectrum_average_band_levels(
            snapshot->combined_levels, AUDIO_SPECTRUM_BAND_COUNT, 8U, 8U),
        audio_spectrum_average_band_levels(
            snapshot->combined_levels, AUDIO_SPECTRUM_BAND_COUNT, 16U, 8U),
    };
    const float target_doa = snapshot->doa_active ?
        clamp_angle(snapshot->relative_angle_deg) * 0.32f : 0.0f;
    float doa_step = (target_doa - state->metaball_doa_offset) * 0.16f;
    if (2.5f < doa_step) {
        doa_step = 2.5f;
    } else if (-2.5f > doa_step) {
        doa_step = -2.5f;
    }
    state->metaball_doa_offset += doa_step;
    const float time_s = (float)now_ms / 1000.0f;
    for (size_t index = 0U; index < HOLO_SPECTRUM_METABALL_COUNT; ++index) {
        const float target = clamp_unit(levels[index]);
        state->metaball_levels[index] +=
            (target - state->metaball_levels[index]) *
            (target > state->metaball_levels[index] ? 0.26f : 0.12f);
        const int radius = base_radius[index] + (int)lroundf(
            state->metaball_levels[index] * (float)radius_range[index]);
        const int center_x = clamp_position(
            base_x[index] + (int)lroundf(sinf(time_s * speed_x[index] +
                                              phase[index]) * amplitude_x[index]) +
            (int)lroundf(state->metaball_doa_offset), 38, 202);
        const int center_y = clamp_position(
            base_y[index] + (int)lroundf(cosf(time_s * speed_y[index] +
                                              phase[index]) * amplitude_y[index]),
            44, 185);
        draw_energy_ball(canvas, center_x, center_y, radius, colors[index]);
    }
}

static const uint8_t s_digit_segments[10] = {
    0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U,
    0x6DU, 0x7DU, 0x07U, 0x7FU, 0x6FU,
};

static void draw_digit(uint16_t *canvas, int x, int y, int scale, int digit,
                       uint16_t color)
{
    if (0 > digit || 9 < digit) return;
    static const int segment_x[7] = {4, 15, 15, 4, 0, 0, 4};
    static const int segment_y[7] = {0, 4, 20, 24, 20, 4, 12};
    static const int segment_w[7] = {12, 4, 4, 12, 4, 4, 12};
    static const int segment_h[7] = {4, 12, 12, 4, 12, 12, 4};
    for (int segment = 0; segment < 7; ++segment) {
        if (0U == (s_digit_segments[digit] & (1U << segment))) continue;
        fill_round_rect(canvas, x + segment_x[segment] * scale,
                        y + segment_y[segment] * scale,
                        segment_w[segment] * scale,
                        segment_h[segment] * scale, 2 * scale, color);
    }
}

static void draw_level_value(uint16_t *canvas, float dbfs)
{
    int value = (int)lroundf(dbfs);
    if (-99 > value) value = -99;
    if (0 < value) value = 0;
    const int magnitude = value < 0 ? -value : value;
    const int scale = 1;
    const int digit_width = 20;
    const int sign_width = 12;
    const int gap = 5;
    const int digits = magnitude >= 10 ? 2 : 1;
    const int total_width = sign_width + gap + digits * digit_width +
                            (digits - 1) * gap;
    int x = (240 - total_width) / 2;
    const uint16_t white = rgb565((raster_rgb_t){RASTER_WHITE_R,
                                                  RASTER_WHITE_G,
                                                  RASTER_WHITE_B});
    if (value < 0) {
        fill_round_rect(canvas, x, 35, sign_width, 4, 2, white);
        x += sign_width + gap;
    }
    if (digits == 2) {
        draw_digit(canvas, x, 24, scale, magnitude / 10, white);
        x += digit_width + gap;
    }
    draw_digit(canvas, x, 24, scale, magnitude % 10, white);
}

static void draw_level(uint16_t *canvas,
                       const holocubic_spectrum_snapshot_t *snapshot,
                       holocubic_spectrum_raster_state_t *state)
{
    const float fast = audio_spectrum_level_from_db(
        snapshot->energy_dbfs, RASTER_LEVEL_MIN_DBFS, RASTER_LEVEL_MAX_DBFS);
    state->slow_level += (fast - state->slow_level) * 0.10f;
    draw_level_value(canvas, snapshot->energy_dbfs);
    draw_horizontal_meter(canvas, 22, 95, 196, 20, fast,
                          (raster_rgb_t){47U, 206U, 255U});
    draw_horizontal_meter(canvas, 22, 139, 196, 10, state->slow_level,
                          (raster_rgb_t){255U, 179U, 69U});
    for (int tick = 1; tick < 8; ++tick) {
        const int x = 22 + tick * 196 / 8;
        draw_line(canvas, x, 91, x, 118,
                  rgb565((raster_rgb_t){40U, 65U, 72U}));
        draw_line(canvas, x, 136, x, 152,
                  rgb565((raster_rgb_t){40U, 65U, 72U}));
    }
    const raster_rgb_t mic1 = {40U, 205U, 255U};
    const raster_rgb_t mic2 = {255U, 158U, 83U};
    draw_dot(canvas, 78, 193, 13, mix_rgb((raster_rgb_t){0U, 0U, 0U}, mic1,
                                           26U));
    draw_dot(canvas, 78, 193, 5 + (int)lroundf(snapshot->mic1_level * 7.0f),
             mic1);
    draw_dot(canvas, 162, 193, 13, mix_rgb((raster_rgb_t){0U, 0U, 0U}, mic2,
                                           26U));
    draw_dot(canvas, 162, 193, 5 + (int)lroundf(snapshot->mic2_level * 7.0f),
             mic2);
    draw_line(canvas, 91, 193, 149, 193,
              rgb565((raster_rgb_t){23U, 48U, 56U}));
}

static void draw_dual(uint16_t *canvas,
                      const holocubic_spectrum_snapshot_t *snapshot)
{
    draw_line(canvas, 11, 20, 228, 20,
              rgb565((raster_rgb_t){31U, 164U, 205U}));
    draw_line(canvas, 11, 223, 228, 223,
              rgb565((raster_rgb_t){204U, 91U, 116U}));
    draw_line(canvas, 11, 120, 228, 120,
              rgb565((raster_rgb_t){25U, 53U, 61U}));
    for (size_t index = 0U; index < AUDIO_SPECTRUM_BAND_COUNT; ++index) {
        const int x = 12 + (int)index * 9;
        const float mic1 = clamp_unit(snapshot->mic1_levels[index]);
        const float mic2 = clamp_unit(snapshot->mic2_levels[index]);
        const int peak1 = 31 + (int)lroundf(
            clamp_unit(snapshot->mic1_peaks[index]) * 80.0f);
        const int peak2 = 130 + (int)lroundf(
            clamp_unit(snapshot->mic2_peaks[index]) * 80.0f);
        draw_vertical_bar(canvas, x, 111, 80, 7, mic1, index, false);
        draw_vertical_bar(canvas, x, 131, 80, 7, mic2, index, true);
        draw_line(canvas, x, 111 - (peak1 - 31), x + 6,
                  111 - (peak1 - 31),
                  rgb565((raster_rgb_t){236U, 249U, 255U}));
        draw_line(canvas, x, 131 + (peak2 - 130), x + 6,
                  131 + (peak2 - 130),
                  rgb565((raster_rgb_t){255U, 213U, 181U}));
    }
}

static void draw_unavailable(uint16_t *canvas)
{
    draw_circle(canvas, RASTER_CENTER_X, 108, 43,
                rgb565((raster_rgb_t){12U, 36U, 46U}));
    draw_circle(canvas, RASTER_CENTER_X, 108, 28,
                rgb565((raster_rgb_t){8U, 23U, 32U}));
    draw_line(canvas, 95, 95, 145, 145,
              rgb565((raster_rgb_t){72U, 106U, 112U}));
    draw_line(canvas, 145, 95, 95, 145,
              rgb565((raster_rgb_t){72U, 106U, 112U}));
    draw_side_meter(canvas, 9, 0.18f, (raster_rgb_t){32U, 92U, 113U});
    draw_side_meter(canvas, 227, 0.18f, (raster_rgb_t){113U, 72U, 47U});
}

void holocubic_spectrum_raster_reset(holocubic_spectrum_raster_state_t *state)
{
    if (NULL != state) memset(state, 0, sizeof(*state));
}

void holocubic_spectrum_raster_draw(
    uint16_t *canvas,
    const holocubic_spectrum_snapshot_t *snapshot,
    holocubic_spectrum_mode_t mode,
    uint32_t now_ms,
    holocubic_spectrum_raster_state_t *state)
{
    holocubic_spectrum_raster_state_t scratch = {0};

    if (NULL == canvas) return;
    if (NULL == state) state = &scratch;
    memset(canvas, RASTER_BLACK,
           HOLO_SPECTRUM_CANVAS_PIXELS * sizeof(uint16_t));
    if (NULL == snapshot || !snapshot->available) {
        draw_unavailable(canvas);
        return;
    }
    update_history(snapshot, state);
    switch (mode) {
    case HOLO_SPECTRUM_MIRROR:
        draw_mirror(canvas, snapshot);
        break;
    case HOLO_SPECTRUM_WATERFALL:
        draw_waterfall(canvas, snapshot, state);
        break;
    case HOLO_SPECTRUM_METABALLS:
        draw_metaballs(canvas, snapshot, now_ms, state);
        break;
    case HOLO_SPECTRUM_LEVEL:
        draw_level(canvas, snapshot, state);
        break;
    case HOLO_SPECTRUM_DUAL:
        draw_dual(canvas, snapshot);
        break;
    case HOLO_SPECTRUM_RADAR:
    default:
        draw_radar(canvas, snapshot, state);
        break;
    }
}
