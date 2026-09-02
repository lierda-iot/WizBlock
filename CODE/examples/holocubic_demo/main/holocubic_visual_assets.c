#include "holocubic_visual_assets.h"

#include <stddef.h>
#include <stdint.h>

#define HOLO_VISUAL_ASSET_BYTES \
    (HOLO_VISUAL_ASSET_PIXELS * sizeof(uint16_t))

extern const uint8_t weather_clear_start[]
    asm("_binary_weather_clear_rgb565_start");
extern const uint8_t weather_clear_end[]
    asm("_binary_weather_clear_rgb565_end");
extern const uint8_t weather_cloudy_start[]
    asm("_binary_weather_cloudy_rgb565_start");
extern const uint8_t weather_cloudy_end[]
    asm("_binary_weather_cloudy_rgb565_end");
extern const uint8_t weather_overcast_start[]
    asm("_binary_weather_overcast_rgb565_start");
extern const uint8_t weather_overcast_end[]
    asm("_binary_weather_overcast_rgb565_end");
extern const uint8_t weather_fog_start[]
    asm("_binary_weather_fog_rgb565_start");
extern const uint8_t weather_fog_end[]
    asm("_binary_weather_fog_rgb565_end");
extern const uint8_t weather_rain_start[]
    asm("_binary_weather_rain_rgb565_start");
extern const uint8_t weather_rain_end[]
    asm("_binary_weather_rain_rgb565_end");
extern const uint8_t weather_snow_start[]
    asm("_binary_weather_snow_rgb565_start");
extern const uint8_t weather_snow_end[]
    asm("_binary_weather_snow_rgb565_end");
extern const uint8_t weather_storm_start[]
    asm("_binary_weather_storm_rgb565_start");
extern const uint8_t weather_storm_end[]
    asm("_binary_weather_storm_rgb565_end");
extern const uint8_t weather_offline_start[]
    asm("_binary_weather_offline_rgb565_start");
extern const uint8_t weather_offline_end[]
    asm("_binary_weather_offline_rgb565_end");
extern const uint8_t clock_orb_start[]
    asm("_binary_clock_orb_rgb565_start");
extern const uint8_t clock_orb_end[]
    asm("_binary_clock_orb_rgb565_end");

static bool set_bitmap(const uint8_t *start, const uint8_t *end,
                       holocubic_visual_bitmap_t *bitmap)
{
    if (NULL == start || NULL == end || NULL == bitmap || end < start ||
        HOLO_VISUAL_ASSET_BYTES != (size_t)(end - start) ||
        0U != ((uintptr_t)start % sizeof(uint16_t))) {
        return false;
    }
    bitmap->pixels = (const uint16_t *)start;
    bitmap->pixel_count = HOLO_VISUAL_ASSET_PIXELS;
    bitmap->width = HOLO_VISUAL_ASSET_WIDTH;
    bitmap->height = HOLO_VISUAL_ASSET_HEIGHT;
    return true;
}

bool holocubic_visual_assets_weather(holocubic_weather_visual_t kind,
                                     holocubic_visual_bitmap_t *bitmap)
{
    switch (kind) {
    case HOLO_WEATHER_VISUAL_CLEAR:
        return set_bitmap(weather_clear_start, weather_clear_end, bitmap);
    case HOLO_WEATHER_VISUAL_CLOUDY:
        return set_bitmap(weather_cloudy_start, weather_cloudy_end, bitmap);
    case HOLO_WEATHER_VISUAL_OVERCAST:
        return set_bitmap(weather_overcast_start, weather_overcast_end,
                          bitmap);
    case HOLO_WEATHER_VISUAL_FOG:
        return set_bitmap(weather_fog_start, weather_fog_end, bitmap);
    case HOLO_WEATHER_VISUAL_RAIN:
        return set_bitmap(weather_rain_start, weather_rain_end, bitmap);
    case HOLO_WEATHER_VISUAL_SNOW:
        return set_bitmap(weather_snow_start, weather_snow_end, bitmap);
    case HOLO_WEATHER_VISUAL_STORM:
        return set_bitmap(weather_storm_start, weather_storm_end, bitmap);
    case HOLO_WEATHER_VISUAL_OFFLINE:
        return set_bitmap(weather_offline_start, weather_offline_end, bitmap);
    default:
        return false;
    }
}

bool holocubic_visual_assets_clock(holocubic_visual_bitmap_t *bitmap)
{
    return set_bitmap(clock_orb_start, clock_orb_end, bitmap);
}
