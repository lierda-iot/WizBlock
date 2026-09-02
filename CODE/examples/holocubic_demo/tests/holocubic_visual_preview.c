#include "holocubic_visual_ui.h"

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static bool read_asset(const char *path, uint16_t *pixels)
{
    FILE *file = NULL;
    size_t read_count = 0U;

    if (NULL == path || NULL == pixels) return false;
    file = fopen(path, "rb");
    if (NULL == file) return false;
    read_count = fread(pixels, sizeof(uint16_t), HOLO_VISUAL_ASSET_PIXELS,
                       file);
    const int trailing = fgetc(file);
    fclose(file);
    return HOLO_VISUAL_ASSET_PIXELS == read_count && EOF == trailing;
}

static bool write_ppm(const char *path, const uint16_t *canvas)
{
    FILE *file = NULL;

    if (NULL == path || NULL == canvas) return false;
    file = fopen(path, "wb");
    if (NULL == file) return false;
    if (0 > fprintf(file, "P6\n%u %u\n255\n", HOLO_VISUAL_CANVAS_WIDTH,
                    HOLO_VISUAL_CANVAS_HEIGHT)) {
        fclose(file);
        return false;
    }
    for (size_t index = 0U; HOLO_VISUAL_CANVAS_PIXELS > index; ++index) {
        const uint16_t pixel = canvas[index];
        const uint8_t red = (uint8_t)((((pixel >> 11) & 0x1FU) * 255U) / 31U);
        const uint8_t green =
            (uint8_t)((((pixel >> 5) & 0x3FU) * 255U) / 63U);
        const uint8_t blue = (uint8_t)(((pixel & 0x1FU) * 255U) / 31U);
        if (EOF == fputc(red, file) || EOF == fputc(green, file) ||
            EOF == fputc(blue, file)) {
            fclose(file);
            return false;
        }
    }
    return 0 == fclose(file);
}

static bool render_weather(const char *asset_path, const char *output_path,
                           uint16_t *asset_pixels, uint16_t *canvas)
{
    holocubic_visual_bitmap_t bitmap = {
        .pixels = asset_pixels,
        .pixel_count = HOLO_VISUAL_ASSET_PIXELS,
        .width = HOLO_VISUAL_ASSET_WIDTH,
        .height = HOLO_VISUAL_ASSET_HEIGHT,
    };
    holocubic_weather_t weather = {
        .state = HOLO_WEATHER_FRESH,
        .temperature_c = 35.5f,
        .high_c = 38.0f,
        .low_c = 28.0f,
        .humidity_percent = 62U,
        .weather_code = 2,
    };

    (void)snprintf(weather.observed_at, sizeof(weather.observed_at),
                   "2026-08-14T12:30");
    return read_asset(asset_path, asset_pixels) &&
           holocubic_visual_draw_weather(canvas, HOLO_VISUAL_CANVAS_PIXELS,
                                          &weather, &bitmap) &&
           write_ppm(output_path, canvas);
}

static bool render_clock(const char *asset_path, const char *output_path,
                         uint16_t *asset_pixels, uint16_t *canvas)
{
    holocubic_visual_bitmap_t bitmap = {
        .pixels = asset_pixels,
        .pixel_count = HOLO_VISUAL_ASSET_PIXELS,
        .width = HOLO_VISUAL_ASSET_WIDTH,
        .height = HOLO_VISUAL_ASSET_HEIGHT,
    };

    return read_asset(asset_path, asset_pixels) &&
           holocubic_visual_draw_clock(canvas, HOLO_VISUAL_CANVAS_PIXELS,
                                        "21:38:42", "2026-08-14", true,
                                        &bitmap) &&
           write_ppm(output_path, canvas);
}

int main(int argc, char **argv)
{
    static uint16_t asset_pixels[HOLO_VISUAL_ASSET_PIXELS];
    static uint16_t canvas[HOLO_VISUAL_CANVAS_PIXELS];
    char asset_path[512] = {0};
    char output_path[512] = {0};

    if (3 != argc) {
        fprintf(stderr, "usage: %s <asset-dir> <output-dir>\n", argv[0]);
        return 2;
    }
    (void)snprintf(asset_path, sizeof(asset_path), "%s/weather_cloudy.rgb565",
                   argv[1]);
    (void)snprintf(output_path, sizeof(output_path), "%s/weather_page.ppm",
                   argv[2]);
    if (!render_weather(asset_path, output_path, asset_pixels, canvas)) {
        return 1;
    }
    (void)snprintf(asset_path, sizeof(asset_path), "%s/clock_orb.rgb565",
                   argv[1]);
    (void)snprintf(output_path, sizeof(output_path), "%s/clock_page.ppm",
                   argv[2]);
    if (!render_clock(asset_path, output_path, asset_pixels, canvas)) {
        return 1;
    }
    return 0;
}
