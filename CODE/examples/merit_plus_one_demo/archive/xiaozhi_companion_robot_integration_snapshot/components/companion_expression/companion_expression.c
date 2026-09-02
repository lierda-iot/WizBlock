#include "companion_expression.h"

#include "companion_expression_catalog.h"
#include "generated/companion_merit_tap_assets.h"
#include "esp_log.h"

#include <stdlib.h>
#include <string.h>

#define COMPANION_FNV1A_OFFSET 2166136261U
#define COMPANION_FNV1A_PRIME 16777619U
#define COMPANION_PERCENT_MAX 100U
#define COMPANION_SATURATION_GAIN_MIN_PERCENT 100U
#define COMPANION_SATURATION_GAIN_MAX_PERCENT 200U
#define COMPANION_RENDER_SCALE_MIN_PERCENT 80U
#define COMPANION_RENDER_SCALE_MAX_PERCENT 125U
#define COMPANION_RENDER_OFFSET_LIMIT_PX 32
#define COMPANION_GRAY_DELTA_MAX 8U
#define COMPANION_SATURATION_LIMIT_MIN_PERCENT 5U
#define COMPANION_RGB565_SATURATION_MARGIN_PERCENT 3U
#define COMPANION_MERIT_BUBBLE_TOTAL_MS 800U

static const char *TAG = "companion_expr";

struct companion_expression {
    companion_expression_config_t config;
    const companion_expression_catalog_t *catalog;
    companion_expression_signals_t signals;
    size_t pack_index;
    size_t rendered_pack_index;
    uint32_t rendered_scene_id;
    uint32_t revision;
    uint64_t next_blink_ms;
    uint64_t blink_deadline_ms;
    uint64_t mouth_deadline_ms;
    uint64_t pout_deadline_ms;
    uint8_t blink_repeats_remaining;
    bool signals_valid;
    bool blink_closed;
    bool mouth_open;
    bool pout_expand;
    bool force_render;
    companion_merit_bubble_signal_t merit_bubble;
    int rendered_merit_frame;
    uint32_t rendered_merit_epoch;
};

static uint32_t hash_id(const char *value)
{
    uint32_t hash = COMPANION_FNV1A_OFFSET;
    if (NULL == value) {
        return 0U;
    }
    while ('\0' != *value) {
        hash ^= (uint8_t)*value;
        hash *= COMPANION_FNV1A_PRIME;
        value++;
    }
    return hash;
}

static const companion_expression_scene_t *find_scene(
    const companion_expression_catalog_t *catalog, uint32_t scene_id)
{
    for (size_t index = 0U; index < catalog->scene_count; ++index) {
        if (scene_id == catalog->scenes[index].scene_id) {
            return &catalog->scenes[index];
        }
    }
    return NULL;
}

static const companion_expression_frame_t *find_direct_frame(
    const companion_expression_pack_t *pack, uint32_t scene_id)
{
    for (size_t index = 0U; index < pack->frame_count; ++index) {
        if (scene_id == pack->frames[index].scene_id) {
            return &pack->frames[index];
        }
    }
    return NULL;
}

static const companion_expression_frame_t *resolve_frame(
    const companion_expression_catalog_t *catalog,
    const companion_expression_pack_t *pack, uint32_t scene_id)
{
    uint32_t current_id = scene_id;
    for (size_t depth = 0U; depth < catalog->scene_count; ++depth) {
        const companion_expression_frame_t *frame =
            find_direct_frame(pack, current_id);
        if (NULL != frame) {
            return frame;
        }
        const companion_expression_scene_t *scene =
            find_scene(catalog, current_id);
        if (NULL == scene ||
            COMPANION_EXPRESSION_NO_SCENE_ID == scene->fallback_scene_id) {
            return NULL;
        }
        current_id = scene->fallback_scene_id;
    }
    return NULL;
}

static esp_err_t validate_config(const companion_expression_config_t *config)
{
    if (NULL == config ||
        0U == config->blink_interval_min_ms ||
        config->blink_interval_min_ms > config->blink_interval_max_ms ||
        0U == config->blink_closed_ms ||
        0U == config->blink_double_gap_ms ||
        COMPANION_PERCENT_MAX < config->blink_double_percent ||
        0U == config->mouth_open_min_ms ||
        config->mouth_open_min_ms > config->mouth_open_max_ms ||
        0U == config->mouth_closed_min_ms ||
        config->mouth_closed_min_ms > config->mouth_closed_max_ms ||
        0U == config->pout_compress_ms || 0U == config->pout_expand_ms ||
        COMPANION_SATURATION_GAIN_MIN_PERCENT >
            config->saturation_gain_percent ||
        COMPANION_SATURATION_GAIN_MAX_PERCENT <
            config->saturation_gain_percent ||
        COMPANION_SATURATION_LIMIT_MIN_PERCENT >
            config->saturation_limit_percent ||
        COMPANION_PERCENT_MAX < config->saturation_limit_percent ||
        COMPANION_RENDER_SCALE_MIN_PERCENT > config->render_scale_percent ||
        COMPANION_RENDER_SCALE_MAX_PERCENT < config->render_scale_percent ||
        -COMPANION_RENDER_OFFSET_LIMIT_PX > config->render_offset_x_px ||
        COMPANION_RENDER_OFFSET_LIMIT_PX < config->render_offset_x_px ||
        -COMPANION_RENDER_OFFSET_LIMIT_PX > config->render_offset_y_px ||
        COMPANION_RENDER_OFFSET_LIMIT_PX < config->render_offset_y_px) {
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static esp_err_t validate_catalog(const companion_expression_catalog_t *catalog)
{
    if (NULL == catalog || NULL == catalog->packs || 0U == catalog->pack_count ||
        NULL == catalog->scenes || 0U == catalog->scene_count) {
        ESP_LOGE(TAG, "catalog empty or incomplete");
        return ESP_ERR_INVALID_ARG;
    }

    bool default_found = false;
    for (size_t scene_index = 0U; scene_index < catalog->scene_count;
         ++scene_index) {
        const companion_expression_scene_t *scene =
            &catalog->scenes[scene_index];
        if (NULL == scene->scene_name || '\0' == scene->scene_name[0] ||
            hash_id(scene->scene_name) != scene->scene_id) {
            ESP_LOGE(TAG, "scene id mismatch index=%u", (unsigned)scene_index);
            return ESP_ERR_INVALID_ARG;
        }
        for (size_t other = 0U; other < scene_index; ++other) {
            if (scene->scene_id == catalog->scenes[other].scene_id) {
                ESP_LOGE(TAG, "duplicate or colliding scene id=%08lx",
                         (unsigned long)scene->scene_id);
                return ESP_ERR_INVALID_ARG;
            }
        }
        if (COMPANION_EXPRESSION_NO_SCENE_ID != scene->fallback_scene_id &&
            NULL == find_scene(catalog, scene->fallback_scene_id)) {
            ESP_LOGE(TAG, "scene=%s has unknown fallback=%08lx",
                     scene->scene_name,
                     (unsigned long)scene->fallback_scene_id);
            return ESP_ERR_INVALID_ARG;
        }

        uint32_t current_id = scene->scene_id;
        for (size_t depth = 0U; depth <= catalog->scene_count; ++depth) {
            const companion_expression_scene_t *current =
                find_scene(catalog, current_id);
            if (NULL == current) {
                return ESP_ERR_INVALID_ARG;
            }
            if (COMPANION_EXPRESSION_NO_SCENE_ID ==
                current->fallback_scene_id) {
                break;
            }
            if (catalog->scene_count == depth) {
                ESP_LOGE(TAG, "fallback cycle starts at scene=%s",
                         scene->scene_name);
                return ESP_ERR_INVALID_ARG;
            }
            current_id = current->fallback_scene_id;
        }
    }

    for (size_t pack_index = 0U; pack_index < catalog->pack_count;
         ++pack_index) {
        const companion_expression_pack_t *pack = &catalog->packs[pack_index];
        if (NULL == pack->pack_name || '\0' == pack->pack_name[0] ||
            NULL == pack->display_name ||
            hash_id(pack->pack_name) != pack->pack_id ||
            NULL == pack->palette || 0U == pack->palette_count ||
            COMPANION_EXPRESSION_PALETTE_LIMIT < pack->palette_count ||
            NULL == pack->frames || 0U == pack->frame_count) {
            ESP_LOGE(TAG, "invalid pack metadata index=%u",
                     (unsigned)pack_index);
            return ESP_ERR_INVALID_ARG;
        }
        if (pack->pack_id == catalog->default_pack_id) {
            default_found = true;
        }
        for (size_t other = 0U; other < pack_index; ++other) {
            if (pack->pack_id == catalog->packs[other].pack_id) {
                ESP_LOGE(TAG, "duplicate or colliding pack id=%08lx",
                         (unsigned long)pack->pack_id);
                return ESP_ERR_INVALID_ARG;
            }
        }
        for (size_t frame_index = 0U; frame_index < pack->frame_count;
             ++frame_index) {
            const companion_expression_frame_t *frame =
                &pack->frames[frame_index];
            if (NULL == find_scene(catalog, frame->scene_id) ||
                NULL == frame->pixels ||
                COMPANION_EXPRESSION_FRAME_BYTES != frame->pixel_bytes) {
                ESP_LOGE(TAG, "invalid frame pack=%s index=%u",
                         pack->pack_name, (unsigned)frame_index);
                return ESP_ERR_INVALID_ARG;
            }
            for (size_t other = 0U; other < frame_index; ++other) {
                if (frame->scene_id == pack->frames[other].scene_id) {
                    ESP_LOGE(TAG, "duplicate frame pack=%s scene=%08lx",
                             pack->pack_name,
                             (unsigned long)frame->scene_id);
                    return ESP_ERR_INVALID_ARG;
                }
            }
            for (size_t byte_index = 0U; byte_index < frame->pixel_bytes;
                 ++byte_index) {
                const uint8_t packed = frame->pixels[byte_index];
                if ((packed & 0x0FU) >= pack->palette_count ||
                    ((packed >> 4U) & 0x0FU) >= pack->palette_count) {
                    ESP_LOGE(TAG,
                             "palette index overflow pack=%s frame=%u byte=%u",
                             pack->pack_name, (unsigned)frame_index,
                             (unsigned)byte_index);
                    return ESP_ERR_INVALID_ARG;
                }
            }
        }
        for (size_t scene_index = 0U; scene_index < catalog->scene_count;
             ++scene_index) {
            const companion_expression_scene_t *scene =
                &catalog->scenes[scene_index];
            if (scene->required &&
                NULL == resolve_frame(catalog, pack, scene->scene_id)) {
                ESP_LOGE(TAG, "required scene unresolved pack=%s scene=%s",
                         pack->pack_name, scene->scene_name);
                return ESP_ERR_INVALID_ARG;
            }
        }
    }
    if (!default_found) {
        ESP_LOGE(TAG, "default pack id not found=%08lx",
                 (unsigned long)catalog->default_pack_id);
        return ESP_ERR_INVALID_ARG;
    }
    return ESP_OK;
}

static size_t find_pack_index(const companion_expression_catalog_t *catalog,
                              uint32_t pack_id)
{
    for (size_t index = 0U; index < catalog->pack_count; ++index) {
        if (pack_id == catalog->packs[index].pack_id) {
            return index;
        }
    }
    return 0U;
}

static uint32_t random_range(uint32_t random_value, uint32_t minimum,
                             uint32_t maximum)
{
    if (minimum == maximum) {
        return minimum;
    }
    return minimum + (random_value % (maximum - minimum + 1U));
}

static uint32_t scene_id(const char *name)
{
    return hash_id(name);
}

static const char *scene_name(const companion_expression_catalog_t *catalog,
                              uint32_t id)
{
    const companion_expression_scene_t *scene = find_scene(catalog, id);
    return (NULL != scene) ? scene->scene_name : "unknown";
}

static const char *product_state_name(companion_product_state_t state)
{
    switch (state) {
    case COMPANION_PRODUCT_BOOTING: return "BOOTING";
    case COMPANION_PRODUCT_WAIT_NETWORK: return "WAIT_NETWORK";
    case COMPANION_PRODUCT_IDLE: return "IDLE";
    case COMPANION_PRODUCT_LOCATING: return "LOCATING";
    case COMPANION_PRODUCT_TURNING: return "TURNING";
    case COMPANION_PRODUCT_CONNECTING: return "CONNECTING";
    case COMPANION_PRODUCT_LISTENING: return "LISTENING";
    case COMPANION_PRODUCT_PROCESSING: return "PROCESSING";
    case COMPANION_PRODUCT_SPEAKING: return "SPEAKING";
    case COMPANION_PRODUCT_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

static const char *turn_direction_name(companion_turn_direction_t direction)
{
    switch (direction) {
    case COMPANION_TURN_NONE: return "NONE";
    case COMPANION_TURN_LEFT: return "LEFT";
    case COMPANION_TURN_RIGHT: return "RIGHT";
    default: return "UNKNOWN";
    }
}

static void reset_animation(companion_expression_t *expression,
                            const companion_expression_signals_t *signals,
                            uint64_t now_ms, uint32_t random_value)
{
    expression->signals = *signals;
    expression->signals_valid = true;
    expression->blink_closed = false;
    expression->blink_repeats_remaining = 0U;
    expression->mouth_open = false;
    expression->pout_expand = false;
    expression->next_blink_ms = now_ms + random_range(
        random_value, expression->config.blink_interval_min_ms,
        expression->config.blink_interval_max_ms);
    expression->mouth_deadline_ms = now_ms + random_range(
        random_value >> 3U, expression->config.mouth_closed_min_ms,
        expression->config.mouth_closed_max_ms);
    expression->pout_deadline_ms = now_ms +
        expression->config.pout_compress_ms;
    expression->merit_bubble = signals->merit_bubble;
    ESP_LOGI(TAG, "signals state=%s(%d) turn=%s(%d) touch=%s pack=%s",
             product_state_name(signals->product_state),
             (int)signals->product_state,
             turn_direction_name(signals->turn_direction),
             (int)signals->turn_direction,
             signals->touch_active ? "on" : "off",
             expression->catalog->packs[expression->pack_index].pack_name);
}

static int merit_bubble_frame(const companion_merit_bubble_signal_t *bubble,
                              uint64_t now_ms)
{
    static const uint32_t boundaries[] = {
        0U, 80U, 160U, 260U, 380U, 510U, 650U, 800U,
    };
    if (NULL == bubble || !bubble->active || now_ms < bubble->start_ms) {
        return -1;
    }
    const uint64_t elapsed = now_ms - bubble->start_ms;
    if (COMPANION_MERIT_BUBBLE_TOTAL_MS <= elapsed) {
        return -1;
    }
    for (size_t index = 0U; index + 1U <
         (sizeof(boundaries) / sizeof(boundaries[0])); ++index) {
        if (elapsed < boundaries[index + 1U]) {
            return (int)index;
        }
    }
    return -1;
}

static void update_idle_animation(companion_expression_t *expression,
                                  uint64_t now_ms, uint32_t random_value)
{
    if (expression->blink_closed && now_ms >= expression->blink_deadline_ms) {
        expression->blink_closed = false;
        if (0U < expression->blink_repeats_remaining) {
            expression->next_blink_ms = now_ms +
                expression->config.blink_double_gap_ms;
        } else {
            expression->next_blink_ms = now_ms + random_range(
                random_value, expression->config.blink_interval_min_ms,
                expression->config.blink_interval_max_ms);
        }
    } else if (!expression->blink_closed &&
               now_ms >= expression->next_blink_ms) {
        expression->blink_closed = true;
        if (0U == expression->blink_repeats_remaining) {
            expression->blink_repeats_remaining =
                ((random_value % COMPANION_PERCENT_MAX) <
                 expression->config.blink_double_percent) ? 1U : 0U;
        } else {
            expression->blink_repeats_remaining--;
        }
        expression->blink_deadline_ms = now_ms +
            expression->config.blink_closed_ms;
    }

}

static void update_active_animation(companion_expression_t *expression,
                                    uint64_t now_ms,
                                    uint32_t random_value)
{
    if (expression->signals.touch_active) {
        if (now_ms >= expression->pout_deadline_ms) {
            expression->pout_expand = !expression->pout_expand;
            expression->pout_deadline_ms = now_ms +
                (expression->pout_expand ? expression->config.pout_expand_ms :
                                           expression->config.pout_compress_ms);
        }
        return;
    }
    if (COMPANION_PRODUCT_SPEAKING == expression->signals.product_state) {
        if (now_ms >= expression->mouth_deadline_ms) {
            expression->mouth_open = !expression->mouth_open;
            expression->mouth_deadline_ms = now_ms +
                (expression->mouth_open ?
                 random_range(random_value,
                              expression->config.mouth_open_min_ms,
                              expression->config.mouth_open_max_ms) :
                 random_range(random_value,
                              expression->config.mouth_closed_min_ms,
                              expression->config.mouth_closed_max_ms));
        }
        return;
    }
    if (COMPANION_PRODUCT_IDLE == expression->signals.product_state ||
        COMPANION_PRODUCT_WAIT_NETWORK ==
            expression->signals.product_state) {
        update_idle_animation(expression, now_ms, random_value);
    }
}

static uint32_t select_scene(const companion_expression_t *expression)
{
    if (expression->signals.touch_active) {
        return scene_id(expression->pout_expand ? "touch_pout_expand" :
                                                   "touch_pout_compress");
    }
    switch (expression->signals.product_state) {
    case COMPANION_PRODUCT_WAIT_NETWORK:
    case COMPANION_PRODUCT_IDLE:
        if (expression->blink_closed) {
            return scene_id("blink");
        }
        return scene_id("idle");
    case COMPANION_PRODUCT_TURNING:
        if (COMPANION_TURN_LEFT == expression->signals.turn_direction) {
            return scene_id("turn_gaze_left");
        }
        if (COMPANION_TURN_RIGHT == expression->signals.turn_direction) {
            return scene_id("turn_gaze_right");
        }
        return scene_id("listen_focus");
    case COMPANION_PRODUCT_SPEAKING:
        return scene_id(expression->mouth_open ? "talk_open" : "talk_closed");
    case COMPANION_PRODUCT_LOCATING:
    case COMPANION_PRODUCT_CONNECTING:
    case COMPANION_PRODUCT_LISTENING:
        return scene_id("listen_focus");
    case COMPANION_PRODUCT_PROCESSING:
    case COMPANION_PRODUCT_ERROR:
        return scene_id("think");
    case COMPANION_PRODUCT_BOOTING:
    default:
        return scene_id("idle");
    }
}

static void clear_surface(companion_rgb565_surface_t *surface)
{
    if (NULL == surface || NULL == surface->pixels) {
        return;
    }
    for (size_t y = 0U; y < surface->height; ++y) {
        memset(&surface->pixels[y * surface->stride_pixels], 0,
               surface->width * sizeof(surface->pixels[0]));
    }
}

static uint8_t channel_max(uint8_t red, uint8_t green, uint8_t blue)
{
    uint8_t result = (red > green) ? red : green;
    return (result > blue) ? result : blue;
}

static uint8_t channel_min(uint8_t red, uint8_t green, uint8_t blue)
{
    uint8_t result = (red < green) ? red : green;
    return (result < blue) ? result : blue;
}

static uint8_t scale_channel_from_max(uint8_t channel, uint8_t maximum,
                                      uint8_t old_delta,
                                      uint16_t new_delta)
{
    const uint16_t distance = (uint16_t)maximum - channel;
    const uint16_t scaled_distance =
        (uint16_t)((distance * new_delta + (old_delta / 2U)) / old_delta);
    return (uint8_t)((uint16_t)maximum - scaled_distance);
}

static uint16_t adjust_saturation(uint16_t color,
                                  const companion_expression_config_t *config)
{
    const uint8_t red5 = (uint8_t)((color >> 11U) & 0x1FU);
    const uint8_t green6 = (uint8_t)((color >> 5U) & 0x3FU);
    const uint8_t blue5 = (uint8_t)(color & 0x1FU);
    const uint8_t red = (uint8_t)((red5 << 3U) | (red5 >> 2U));
    const uint8_t green = (uint8_t)((green6 << 2U) | (green6 >> 4U));
    const uint8_t blue = (uint8_t)((blue5 << 3U) | (blue5 >> 2U));
    const uint8_t maximum = channel_max(red, green, blue);
    const uint8_t minimum = channel_min(red, green, blue);
    const uint8_t old_delta = maximum - minimum;
    if (COMPANION_GRAY_DELTA_MAX >= old_delta || 0U == maximum) {
        return color;
    }

    uint16_t new_delta = (uint16_t)(
        ((uint32_t)old_delta * config->saturation_gain_percent + 50U) / 100U);
    const uint8_t quantized_limit = (uint8_t)(
        config->saturation_limit_percent -
        COMPANION_RGB565_SATURATION_MARGIN_PERCENT);
    const uint16_t maximum_delta = (uint16_t)(
        ((uint32_t)maximum * quantized_limit + 50U) / 100U);
    if (maximum_delta < new_delta) {
        new_delta = maximum_delta;
    }

    const uint8_t adjusted_red = scale_channel_from_max(
        red, maximum, old_delta, new_delta);
    const uint8_t adjusted_green = scale_channel_from_max(
        green, maximum, old_delta, new_delta);
    const uint8_t adjusted_blue = scale_channel_from_max(
        blue, maximum, old_delta, new_delta);
    return (uint16_t)((((uint16_t)adjusted_red >> 3U) << 11U) |
                      (((uint16_t)adjusted_green >> 2U) << 5U) |
                      ((uint16_t)adjusted_blue >> 3U));
}

static void fill_surface(companion_rgb565_surface_t *surface, uint16_t color)
{
    for (size_t y = 0U; y < surface->height; ++y) {
        uint16_t *row = &surface->pixels[y * surface->stride_pixels];
        for (size_t x = 0U; x < surface->width; ++x) {
            row[x] = color;
        }
    }
}

static void expand_frame(const companion_expression_pack_t *pack,
                         const companion_expression_frame_t *frame,
                         const companion_expression_config_t *config,
                         companion_rgb565_surface_t *surface)
{
    uint16_t adjusted_palette[COMPANION_EXPRESSION_PALETTE_LIMIT] = {0};
    adjusted_palette[0] = pack->palette[0];
    for (size_t index = 1U; index < pack->palette_count; ++index) {
        adjusted_palette[index] = adjust_saturation(pack->palette[index], config);
    }

    fill_surface(surface, adjusted_palette[0]);
    const int32_t target_width = (int32_t)(
        (surface->width * config->render_scale_percent) / 100U);
    const int32_t target_height = (int32_t)(
        (surface->height * config->render_scale_percent) / 100U);
    const int32_t target_x =
        ((int32_t)surface->width - target_width) / 2 +
        config->render_offset_x_px;
    const int32_t target_y =
        ((int32_t)surface->height - target_height) / 2 +
        config->render_offset_y_px;

    for (size_t output_y = 0U; output_y < surface->height; ++output_y) {
        const int32_t relative_y = (int32_t)output_y - target_y;
        if (0 > relative_y || target_height <= relative_y) {
            continue;
        }
        const size_t logical_y = (size_t)(
            (relative_y * (int32_t)COMPANION_EXPRESSION_LOGICAL_HEIGHT) /
            target_height);
        uint16_t *row = &surface->pixels[output_y * surface->stride_pixels];
        for (size_t output_x = 0U; output_x < surface->width; ++output_x) {
            const int32_t relative_x = (int32_t)output_x - target_x;
            if (0 > relative_x || target_width <= relative_x) {
                continue;
            }
            const size_t logical_x = (size_t)(
                (relative_x * (int32_t)COMPANION_EXPRESSION_LOGICAL_WIDTH) /
                target_width);
            const size_t logical_index = logical_y *
                COMPANION_EXPRESSION_LOGICAL_WIDTH + logical_x;
            const uint8_t packed = frame->pixels[logical_index / 2U];
            const uint8_t palette_index = (0U == (logical_index & 1U)) ?
                (packed & 0x0FU) : ((packed >> 4U) & 0x0FU);
            row[output_x] = adjusted_palette[palette_index];
        }
    }
}

static uint16_t blend_rgb565(uint16_t background, uint16_t foreground,
                             uint8_t alpha)
{
    if (0U == alpha) {
        return background;
    }
    if (UINT8_MAX == alpha) {
        return foreground;
    }
    const uint32_t inverse = UINT8_MAX - alpha;
    const uint32_t bg_red = (background >> 11U) & 0x1FU;
    const uint32_t bg_green = (background >> 5U) & 0x3FU;
    const uint32_t bg_blue = background & 0x1FU;
    const uint32_t fg_red = (foreground >> 11U) & 0x1FU;
    const uint32_t fg_green = (foreground >> 5U) & 0x3FU;
    const uint32_t fg_blue = foreground & 0x1FU;
    return (uint16_t)(((((bg_red * inverse) + (fg_red * alpha) + 127U) /
                        UINT8_MAX) << 11U) |
                      ((((bg_green * inverse) + (fg_green * alpha) + 127U) /
                        UINT8_MAX) << 5U) |
                      (((bg_blue * inverse) + (fg_blue * alpha) + 127U) /
                       UINT8_MAX));
}

static void draw_merit_bubble(const companion_merit_bubble_signal_t *bubble,
                              uint64_t now_ms,
                              companion_rgb565_surface_t *surface)
{
    const int frame_index = merit_bubble_frame(bubble, now_ms);
    if (frame_index < 0 || NULL == surface || NULL == surface->pixels) {
        return;
    }
    const companion_merit_bubble_asset_t *asset =
        &g_companion_merit_bubble_assets[(size_t)frame_index];
    for (size_t y = 0U; y < COMPANION_MERIT_BUBBLE_TILE_HEIGHT; ++y) {
        const size_t output_y = COMPANION_MERIT_BUBBLE_TILE_ORIGIN_Y + y;
        if (surface->height <= output_y) {
            continue;
        }
        for (size_t x = 0U; x < COMPANION_MERIT_BUBBLE_TILE_WIDTH; ++x) {
            const size_t output_x = COMPANION_MERIT_BUBBLE_TILE_ORIGIN_X + x;
            if (surface->width <= output_x) {
                continue;
            }
            const size_t index = y * COMPANION_MERIT_BUBBLE_TILE_WIDTH + x;
            const uint8_t alpha = asset->alpha[index];
            if (0U != alpha) {
                uint16_t *pixel = &surface->pixels[
                    output_y * surface->stride_pixels + output_x];
                *pixel = blend_rgb565(*pixel, asset->pixels[index], alpha);
            }
        }
    }
}

void companion_expression_config_default(companion_expression_config_t *config)
{
    if (NULL == config) {
        return;
    }
    *config = (companion_expression_config_t) {
        .blink_interval_min_ms = 3000U,
        .blink_interval_max_ms = 8000U,
        .blink_closed_ms = 150U,
        .blink_double_gap_ms = 100U,
        .blink_double_percent = 20U,
        .mouth_open_min_ms = 100U,
        .mouth_open_max_ms = 220U,
        .mouth_closed_min_ms = 80U,
        .mouth_closed_max_ms = 260U,
        .pout_compress_ms = 220U,
        .pout_expand_ms = 260U,
        .saturation_gain_percent = 160U,
        .saturation_limit_percent = 85U,
        .render_scale_percent = 110U,
        .render_offset_x_px = 0,
        .render_offset_y_px = -8,
    };
}

esp_err_t companion_expression_open(
    const companion_expression_config_t *config,
    companion_expression_t **expression)
{
    if (NULL == expression) {
        return ESP_ERR_INVALID_ARG;
    }
    *expression = NULL;
    esp_err_t result = validate_config(config);
    if (ESP_OK != result) {
        return result;
    }
    result = validate_catalog(&g_companion_expression_catalog);
    if (ESP_OK != result) {
        return result;
    }
    companion_expression_t *created = calloc(1U, sizeof(*created));
    if (NULL == created) {
        return ESP_ERR_NO_MEM;
    }
    created->config = *config;
    created->catalog = &g_companion_expression_catalog;
    created->pack_index = find_pack_index(created->catalog,
                                          created->catalog->default_pack_id);
    created->rendered_pack_index = created->catalog->pack_count;
    created->force_render = true;
    *expression = created;
    ESP_LOGI(TAG, "catalog ready packs=%u scenes=%u default=%s frame_bytes=%u",
             (unsigned)created->catalog->pack_count,
             (unsigned)created->catalog->scene_count,
             created->catalog->packs[created->pack_index].pack_name,
             COMPANION_EXPRESSION_FRAME_BYTES);
    ESP_LOGI(TAG,
             "renderer color_gain=%u%% color_cap=%u%% scale=%u%% offset=(%d,%d)",
             config->saturation_gain_percent,
             config->saturation_limit_percent,
             config->render_scale_percent,
             config->render_offset_x_px,
             config->render_offset_y_px);
    return ESP_OK;
}

void companion_expression_close(companion_expression_t *expression)
{
    free(expression);
}

size_t companion_expression_pack_count(
    const companion_expression_t *expression)
{
    return (NULL != expression) ? expression->catalog->pack_count : 0U;
}

esp_err_t companion_expression_step_pack(
    companion_expression_t *expression,
    companion_pack_step_t step)
{
    if (NULL == expression ||
        (COMPANION_PACK_PREVIOUS != step && COMPANION_PACK_NEXT != step)) {
        return ESP_ERR_INVALID_ARG;
    }
    const size_t count = expression->catalog->pack_count;
    if (1U < count) {
        expression->pack_index = (COMPANION_PACK_NEXT == step) ?
            ((expression->pack_index + 1U) % count) :
            ((expression->pack_index + count - 1U) % count);
        expression->force_render = true;
    }
    ESP_LOGI(TAG, "pack step=%s index=%u/%u id=%s",
             (COMPANION_PACK_NEXT == step) ? "next" : "previous",
             (unsigned)(expression->pack_index + 1U), (unsigned)count,
             expression->catalog->packs[expression->pack_index].pack_name);
    return ESP_OK;
}

esp_err_t companion_expression_render(
    companion_expression_t *expression,
    const companion_expression_signals_t *signals,
    uint64_t now_ms,
    uint32_t random_value,
    companion_rgb565_surface_t *surface,
    companion_expression_result_t *result)
{
    if (NULL == surface || NULL == surface->pixels ||
        COMPANION_EXPRESSION_SURFACE_WIDTH != surface->width ||
        COMPANION_EXPRESSION_SURFACE_HEIGHT != surface->height ||
        surface->width > surface->stride_pixels) {
        return ESP_ERR_INVALID_ARG;
    }
    if (NULL == expression || NULL == signals || NULL == result ||
        COMPANION_PRODUCT_STATE_COUNT <= signals->product_state ||
        COMPANION_TURN_DIRECTION_COUNT <= signals->turn_direction) {
        clear_surface(surface);
        return ESP_ERR_INVALID_ARG;
    }

    if (!expression->signals_valid ||
        expression->signals.product_state != signals->product_state ||
        expression->signals.turn_direction != signals->turn_direction ||
        expression->signals.touch_active != signals->touch_active) {
        reset_animation(expression, signals, now_ms, random_value);
    }
    update_active_animation(expression, now_ms, random_value);
    const uint32_t selected_scene_id = select_scene(expression);
    const companion_expression_pack_t *pack =
        &expression->catalog->packs[expression->pack_index];
    const companion_expression_frame_t *frame = resolve_frame(
        expression->catalog, pack, selected_scene_id);
    if (NULL == frame) {
        clear_surface(surface);
        ESP_LOGE(TAG, "frame unresolved pack=%s scene=%s",
                 pack->pack_name,
                 scene_name(expression->catalog, selected_scene_id));
        return ESP_ERR_NOT_FOUND;
    }

    const int current_merit_frame = merit_bubble_frame(
        &signals->merit_bubble, now_ms);
    const bool merit_changed =
        expression->merit_bubble.epoch != signals->merit_bubble.epoch ||
        expression->rendered_merit_frame != current_merit_frame ||
        expression->rendered_merit_epoch != signals->merit_bubble.epoch;
    const bool changed = expression->force_render ||
        selected_scene_id != expression->rendered_scene_id ||
        expression->pack_index != expression->rendered_pack_index ||
        merit_changed;
    if (changed) {
        expand_frame(pack, frame, &expression->config, surface);
        draw_merit_bubble(&signals->merit_bubble, now_ms, surface);
        expression->rendered_scene_id = selected_scene_id;
        expression->rendered_pack_index = expression->pack_index;
        expression->merit_bubble = signals->merit_bubble;
        expression->rendered_merit_frame = current_merit_frame;
        expression->rendered_merit_epoch = signals->merit_bubble.epoch;
        expression->revision++;
        expression->force_render = false;
        ESP_LOGD(TAG, "render revision=%lu pack=%s scene=%s",
                 (unsigned long)expression->revision, pack->pack_name,
                 scene_name(expression->catalog, selected_scene_id));
    }
    *result = (companion_expression_result_t) {
        .changed = changed,
        .revision = expression->revision,
        .current_pack_id = pack->pack_name,
    };
    return ESP_OK;
}
