#include "holocubic_weather.h"

#include "third_party/jsmn.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#define HOLO_WEATHER_MAX_TOKENS 96U
#define HOLO_WEATHER_NUMBER_MAX_BYTES 31U

static bool token_equals(const char *json, const jsmntok_t *token,
                         const char *expected)
{
    size_t token_length = 0U;
    size_t expected_length = 0U;

    if (NULL == json || NULL == token || NULL == expected ||
        JSMN_STRING != token->type || token->start < 0 ||
        token->end < token->start) {
        return false;
    }
    token_length = (size_t)(token->end - token->start);
    expected_length = strlen(expected);
    return token_length == expected_length &&
           0 == memcmp(json + token->start, expected, token_length);
}

static bool find_object_value(const char *json, const jsmntok_t *tokens,
                              int token_count, int object_index,
                              const char *key, int *value_index)
{
    int match_index = -1;
    int match_count = 0;

    if (NULL == json || NULL == tokens || NULL == key || NULL == value_index ||
        object_index < 0 || object_index >= token_count ||
        JSMN_OBJECT != tokens[object_index].type) {
        return false;
    }
    for (int index = object_index + 1; index < token_count; ++index) {
        if (tokens[index].parent == object_index &&
            token_equals(json, &tokens[index], key)) {
            match_count++;
            match_index = index;
        }
    }
    if (1 != match_count) {
        return false;
    }
    for (int index = match_index + 1; index < token_count; ++index) {
        if (tokens[index].parent == match_index) {
            *value_index = index;
            return true;
        }
    }
    return false;
}

static bool parse_number(const char *json, const jsmntok_t *token,
                         double *value)
{
    char number[HOLO_WEATHER_NUMBER_MAX_BYTES + 1U] = {0};
    char *end = NULL;
    size_t length = 0U;

    if (NULL == json || NULL == token || NULL == value ||
        JSMN_PRIMITIVE != token->type || token->start < 0 ||
        token->end <= token->start) {
        return false;
    }
    length = (size_t)(token->end - token->start);
    if (length > HOLO_WEATHER_NUMBER_MAX_BYTES) {
        return false;
    }
    memcpy(number, json + token->start, length);
    *value = strtod(number, &end);
    return end == number + length;
}

static bool parse_string(const char *json, const jsmntok_t *token,
                         char *out, size_t out_size)
{
    size_t length = 0U;

    if (NULL == json || NULL == token || NULL == out || 0U == out_size ||
        JSMN_STRING != token->type || token->start < 0 ||
        token->end <= token->start) {
        return false;
    }
    length = (size_t)(token->end - token->start);
    if (length >= out_size || NULL != memchr(json + token->start, '\\', length)) {
        return false;
    }
    memcpy(out, json + token->start, length);
    out[length] = '\0';
    return true;
}

static bool parse_array_first_number(const char *json,
                                     const jsmntok_t *tokens,
                                     int token_count, int array_index,
                                     double *value)
{
    if (NULL == tokens || array_index < 0 || array_index >= token_count ||
        JSMN_ARRAY != tokens[array_index].type || 0 == tokens[array_index].size) {
        return false;
    }
    for (int index = array_index + 1; index < token_count; ++index) {
        if (tokens[index].parent == array_index) {
            return parse_number(json, &tokens[index], value);
        }
    }
    return false;
}

static bool is_json_root_complete(const char *json, size_t json_length,
                                  const jsmntok_t *tokens, int token_count)
{
    int root_count = 0;

    if (token_count < 1 || JSMN_OBJECT != tokens[0].type ||
        0 != tokens[0].start || tokens[0].end <= 0) {
        return false;
    }
    for (int index = 0; index < token_count; ++index) {
        if (-1 == tokens[index].parent) {
            root_count++;
        }
    }
    for (size_t index = (size_t)tokens[0].end; index < json_length; ++index) {
        if (' ' != json[index] && '\t' != json[index] &&
            '\r' != json[index] && '\n' != json[index]) {
            return false;
        }
    }
    return 1 == root_count;
}

static bool valid_weather_code(int16_t code)
{
    switch (code) {
    case 0: case 1: case 2: case 3: case 45: case 48:
    case 51: case 53: case 55: case 56: case 57:
    case 61: case 63: case 65: case 66: case 67:
    case 71: case 73: case 75: case 77: case 80:
    case 81: case 82: case 85: case 86: case 95:
    case 96: case 99:
        return true;
    default:
        return false;
    }
}

bool holocubic_weather_parse(const char *json, size_t json_length,
                             uint64_t fetched_at_ms,
                             holocubic_weather_t *out)
{
    jsmn_parser parser = {0};
    jsmntok_t tokens[HOLO_WEATHER_MAX_TOKENS] = {0};
    int token_count = 0;
    int current_index = -1;
    int daily_index = -1;
    int value_index = -1;
    double temperature = 0.0;
    double high = 0.0;
    double low = 0.0;
    double humidity = 0.0;
    double code = 0.0;
    holocubic_weather_t parsed = {0};

    if (NULL == json || NULL == out || 0U == json_length ||
        json_length > HOLO_WEATHER_JSON_MAX_BYTES) {
        return false;
    }
    jsmn_init(&parser);
    token_count = jsmn_parse(&parser, json, json_length, tokens,
                             HOLO_WEATHER_MAX_TOKENS);
    if (!is_json_root_complete(json, json_length, tokens, token_count) ||
        !find_object_value(json, tokens, token_count, 0, "current",
                           &current_index) ||
        JSMN_OBJECT != tokens[current_index].type ||
        !find_object_value(json, tokens, token_count, 0, "daily",
                           &daily_index) ||
        JSMN_OBJECT != tokens[daily_index].type ||
        !find_object_value(json, tokens, token_count, current_index,
                           "temperature_2m", &value_index) ||
        !parse_number(json, &tokens[value_index], &temperature) ||
        !find_object_value(json, tokens, token_count, current_index,
                           "relative_humidity_2m", &value_index) ||
        !parse_number(json, &tokens[value_index], &humidity) ||
        !find_object_value(json, tokens, token_count, current_index,
                           "weather_code", &value_index) ||
        !parse_number(json, &tokens[value_index], &code) ||
        !find_object_value(json, tokens, token_count, current_index,
                           "time", &value_index) ||
        !parse_string(json, &tokens[value_index], parsed.observed_at,
                      sizeof(parsed.observed_at)) ||
        !find_object_value(json, tokens, token_count, daily_index,
                           "temperature_2m_max", &value_index) ||
        !parse_array_first_number(json, tokens, token_count, value_index,
                                  &high) ||
        !find_object_value(json, tokens, token_count, daily_index,
                           "temperature_2m_min", &value_index) ||
        !parse_array_first_number(json, tokens, token_count, value_index,
                                  &low)) {
        return false;
    }
    if (temperature < -80.0 || temperature > 60.0 ||
        high < -80.0 || high > 60.0 || low < -80.0 || low > 60.0 ||
        humidity < 0.0 || humidity > 100.0 ||
        code < INT16_MIN || code > INT16_MAX ||
        code != (double)(int16_t)code || !valid_weather_code((int16_t)code) ||
        high < low) {
        return false;
    }
    parsed.state = HOLO_WEATHER_FRESH;
    parsed.temperature_c = (float)temperature;
    parsed.high_c = (float)high;
    parsed.low_c = (float)low;
    parsed.humidity_percent = (uint8_t)(humidity + 0.5);
    parsed.weather_code = (int16_t)code;
    parsed.fetched_at_ms = fetched_at_ms;
    parsed.revision = 1U;
    *out = parsed;
    return true;
}

const char *holocubic_weather_code_text(int16_t weather_code)
{
    if (0 == weather_code) return "CLEAR";
    if (1 == weather_code || 2 == weather_code) return "CLOUDY";
    if (3 == weather_code) return "OVERCAST";
    if (45 == weather_code || 48 == weather_code) return "FOG";
    if (weather_code >= 51 && weather_code <= 67) return "DRIZZLE";
    if (weather_code >= 71 && weather_code <= 86) return "SNOW";
    if (weather_code >= 95) return "STORM";
    return "WEATHER";
}

void holocubic_weather_mark_stale(holocubic_weather_t *weather, uint64_t now_ms)
{
    if (NULL != weather && HOLO_WEATHER_FRESH == weather->state &&
        now_ms >= weather->fetched_at_ms &&
        now_ms - weather->fetched_at_ms > 30ULL * 60ULL * 1000ULL) {
        weather->state = HOLO_WEATHER_STALE;
        weather->revision++;
    }
}
