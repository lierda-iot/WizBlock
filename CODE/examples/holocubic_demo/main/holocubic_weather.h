#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#define HOLO_WEATHER_JSON_MAX_BYTES 4096U
#define HOLO_WEATHER_TEXT_MAX_BYTES 24U

typedef enum {
    HOLO_WEATHER_OFFLINE = 0,
    HOLO_WEATHER_FRESH,
    HOLO_WEATHER_STALE,
} holocubic_weather_state_t;

typedef struct {
    holocubic_weather_state_t state;
    float temperature_c;
    float high_c;
    float low_c;
    uint8_t humidity_percent;
    int16_t weather_code;
    char observed_at[24];
    uint64_t fetched_at_ms;
    uint32_t revision;
} holocubic_weather_t;

bool holocubic_weather_parse(const char *json, size_t json_length,
                             uint64_t fetched_at_ms,
                             holocubic_weather_t *out);
const char *holocubic_weather_code_text(int16_t weather_code);
void holocubic_weather_mark_stale(holocubic_weather_t *weather,
                                  uint64_t now_ms);
