#include "notifier_font.h"

#include "esp_log.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

static const char *TAG = "notifier_font";

#define NOTIFIER_FONT_MAGIC_SIZE          8U
#define NOTIFIER_FONT_VERSION             1U
#define NOTIFIER_FONT_FIXED_HEADER_SIZE   36U
#define NOTIFIER_FONT_RANGE_RECORD_SIZE   12U
#define NOTIFIER_FONT_METRIC_RECORD_SIZE  12U
#define NOTIFIER_FONT_LINE_HEIGHT         20U
#define NOTIFIER_FONT_BASE_LINE           5U
#define NOTIFIER_FONT_BPP                 2U
#define NOTIFIER_FONT_MAX_RANGES          4U
#define NOTIFIER_FONT_EXPECTED_GLYPHS     21247U

#define NOTIFIER_FONT_HEADER_VERSION      8U
#define NOTIFIER_FONT_HEADER_SIZE         10U
#define NOTIFIER_FONT_HEADER_GLYPH_COUNT  12U
#define NOTIFIER_FONT_HEADER_METRICS      16U
#define NOTIFIER_FONT_HEADER_BITMAPS      20U
#define NOTIFIER_FONT_HEADER_FILE_SIZE    24U
#define NOTIFIER_FONT_HEADER_LINE_HEIGHT  28U
#define NOTIFIER_FONT_HEADER_BASE_LINE    29U
#define NOTIFIER_FONT_HEADER_BPP          30U
#define NOTIFIER_FONT_HEADER_RANGE_COUNT  31U
#define NOTIFIER_FONT_HEADER_METRIC_SIZE  32U

#define NOTIFIER_FONT_METRIC_BITMAP       0U
#define NOTIFIER_FONT_METRIC_ADVANCE      4U
#define NOTIFIER_FONT_METRIC_WIDTH        6U
#define NOTIFIER_FONT_METRIC_HEIGHT       7U
#define NOTIFIER_FONT_METRIC_OFS_X        8U
#define NOTIFIER_FONT_METRIC_OFS_Y        9U

typedef struct {
    const uint8_t *data;
    size_t data_size;
    uint32_t glyph_count;
    uint32_t metrics_offset;
    uint32_t bitmap_offset;
    uint8_t range_count;
    bool ready;
} notifier_font_state_t;

extern const uint8_t g_notifier_font_binary_start[]
    asm("_binary_notifier_noto_sans_sc_16_bin_start");
extern const uint8_t g_notifier_font_binary_end[]
    asm("_binary_notifier_noto_sans_sc_16_bin_end");

static notifier_font_state_t s_font_state;
static const uint8_t s_expected_magic[NOTIFIER_FONT_MAGIC_SIZE] = {
    'N', 'T', 'F', 'N', '1', '6', 'B', '2',
};

static uint16_t read_u16_le(const uint8_t *value)
{
    if (NULL == value) {
        return 0U;
    }
    return (uint16_t)value[0] | ((uint16_t)value[1] << 8U);
}

static uint32_t read_u32_le(const uint8_t *value)
{
    if (NULL == value) {
        return 0U;
    }
    return (uint32_t)value[0] |
           ((uint32_t)value[1] << 8U) |
           ((uint32_t)value[2] << 16U) |
           ((uint32_t)value[3] << 24U);
}

static bool find_glyph_index(uint32_t letter, uint32_t *glyph_index)
{
    if (!s_font_state.ready || NULL == glyph_index) {
        return false;
    }
    for (uint8_t index = 0U; index < s_font_state.range_count; ++index) {
        const uint8_t *range = s_font_state.data +
                               NOTIFIER_FONT_FIXED_HEADER_SIZE +
                               ((size_t)index * NOTIFIER_FONT_RANGE_RECORD_SIZE);
        uint32_t range_start = read_u32_le(range);
        uint32_t range_end = read_u32_le(range + 4U);
        uint32_t first_index = read_u32_le(range + 8U);

        if (letter >= range_start && letter <= range_end) {
            *glyph_index = first_index + letter - range_start;
            return true;
        }
    }
    return false;
}

static const uint8_t *get_metric(uint32_t glyph_index)
{
    return s_font_state.data + s_font_state.metrics_offset +
           ((size_t)glyph_index * NOTIFIER_FONT_METRIC_RECORD_SIZE);
}

static bool get_glyph_descriptor(const lv_font_t *font,
                                 lv_font_glyph_dsc_t *descriptor,
                                 uint32_t letter,
                                 uint32_t next_letter)
{
    uint32_t glyph_index = 0U;
    const uint8_t *metric = NULL;

    (void)next_letter;
    if (NULL == font || NULL == descriptor ||
        !find_glyph_index(letter, &glyph_index)) {
        return false;
    }
    metric = get_metric(glyph_index);
    descriptor->adv_w = read_u16_le(metric + NOTIFIER_FONT_METRIC_ADVANCE);
    descriptor->box_w = metric[NOTIFIER_FONT_METRIC_WIDTH];
    descriptor->box_h = metric[NOTIFIER_FONT_METRIC_HEIGHT];
    descriptor->ofs_x = (int8_t)metric[NOTIFIER_FONT_METRIC_OFS_X];
    descriptor->ofs_y = (int8_t)metric[NOTIFIER_FONT_METRIC_OFS_Y];
    descriptor->bpp = NOTIFIER_FONT_BPP;
    descriptor->is_placeholder = false;
    return true;
}

static const uint8_t *get_glyph_bitmap(const lv_font_t *font,
                                       uint32_t letter)
{
    uint32_t glyph_index = 0U;
    const uint8_t *metric = NULL;

    if (NULL == font || !find_glyph_index(letter, &glyph_index)) {
        return NULL;
    }
    metric = get_metric(glyph_index);
    return s_font_state.data + s_font_state.bitmap_offset +
           read_u32_le(metric + NOTIFIER_FONT_METRIC_BITMAP);
}

const lv_font_t g_notifier_font_noto_sans_sc_16 = {
    .get_glyph_dsc = get_glyph_descriptor,
    .get_glyph_bitmap = get_glyph_bitmap,
    .line_height = NOTIFIER_FONT_LINE_HEIGHT,
    .base_line = NOTIFIER_FONT_BASE_LINE,
    .subpx = LV_FONT_SUBPX_NONE,
    .underline_position = -2,
    .underline_thickness = 1,
    .dsc = &s_font_state,
    .fallback = NULL,
#if LV_USE_USER_DATA
    .user_data = NULL,
#endif
};

static bool validate_ranges(const uint8_t *data, uint8_t range_count,
                            uint32_t glyph_count)
{
    uint32_t next_glyph_index = 0U;

    if (NULL == data) {
        return false;
    }
    for (uint8_t index = 0U; index < range_count; ++index) {
        const uint8_t *range = data + NOTIFIER_FONT_FIXED_HEADER_SIZE +
                               ((size_t)index * NOTIFIER_FONT_RANGE_RECORD_SIZE);
        uint32_t range_start = read_u32_le(range);
        uint32_t range_end = read_u32_le(range + 4U);
        uint32_t first_index = read_u32_le(range + 8U);

        if (range_end < range_start || first_index != next_glyph_index) {
            return false;
        }
        next_glyph_index += range_end - range_start + 1U;
    }
    return next_glyph_index == glyph_count;
}

static bool validate_metrics(const uint8_t *data, uint32_t glyph_count,
                             uint32_t metrics_offset, uint32_t bitmap_offset,
                             uint32_t file_size)
{
    uint32_t previous_end = 0U;

    if (NULL == data || bitmap_offset > file_size) {
        return false;
    }
    for (uint32_t index = 0U; index < glyph_count; ++index) {
        const uint8_t *metric = data + metrics_offset +
                                ((size_t)index * NOTIFIER_FONT_METRIC_RECORD_SIZE);
        uint32_t bitmap_relative = read_u32_le(
            metric + NOTIFIER_FONT_METRIC_BITMAP);
        uint32_t width = metric[NOTIFIER_FONT_METRIC_WIDTH];
        uint32_t height = metric[NOTIFIER_FONT_METRIC_HEIGHT];
        uint32_t bitmap_size = ((width * height * NOTIFIER_FONT_BPP) + 7U) / 8U;

        if (bitmap_relative < previous_end ||
            bitmap_relative > file_size - bitmap_offset ||
            bitmap_size > (file_size - bitmap_offset - bitmap_relative)) {
            return false;
        }
        previous_end = bitmap_relative + bitmap_size;
    }
    return previous_end == file_size - bitmap_offset;
}

esp_err_t notifier_font_init(void)
{
    const uint8_t *data = g_notifier_font_binary_start;
    size_t data_size = (size_t)((uintptr_t)g_notifier_font_binary_end -
                                (uintptr_t)g_notifier_font_binary_start);
    uint16_t header_size = 0U;
    uint32_t glyph_count = 0U;
    uint32_t metrics_offset = 0U;
    uint32_t bitmap_offset = 0U;
    uint32_t file_size = 0U;
    uint8_t range_count = 0U;

    memset(&s_font_state, 0, sizeof(s_font_state));
    if (data_size < NOTIFIER_FONT_FIXED_HEADER_SIZE ||
        0 != memcmp(data, s_expected_magic, sizeof(s_expected_magic))) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    header_size = read_u16_le(data + NOTIFIER_FONT_HEADER_SIZE);
    glyph_count = read_u32_le(data + NOTIFIER_FONT_HEADER_GLYPH_COUNT);
    metrics_offset = read_u32_le(data + NOTIFIER_FONT_HEADER_METRICS);
    bitmap_offset = read_u32_le(data + NOTIFIER_FONT_HEADER_BITMAPS);
    file_size = read_u32_le(data + NOTIFIER_FONT_HEADER_FILE_SIZE);
    range_count = data[NOTIFIER_FONT_HEADER_RANGE_COUNT];

    if (NOTIFIER_FONT_VERSION !=
            read_u16_le(data + NOTIFIER_FONT_HEADER_VERSION) ||
        NOTIFIER_FONT_LINE_HEIGHT != data[NOTIFIER_FONT_HEADER_LINE_HEIGHT] ||
        NOTIFIER_FONT_BASE_LINE != data[NOTIFIER_FONT_HEADER_BASE_LINE] ||
        NOTIFIER_FONT_BPP != data[NOTIFIER_FONT_HEADER_BPP] ||
        NOTIFIER_FONT_METRIC_RECORD_SIZE !=
            data[NOTIFIER_FONT_HEADER_METRIC_SIZE] ||
        0U == range_count || NOTIFIER_FONT_MAX_RANGES < range_count ||
        NOTIFIER_FONT_EXPECTED_GLYPHS != glyph_count ||
        data_size != file_size) {
        return ESP_ERR_INVALID_RESPONSE;
    }
    if (header_size != NOTIFIER_FONT_FIXED_HEADER_SIZE +
                           ((uint16_t)range_count * NOTIFIER_FONT_RANGE_RECORD_SIZE) ||
        header_size > file_size || metrics_offset != header_size ||
        bitmap_offset < metrics_offset +
                            (glyph_count * NOTIFIER_FONT_METRIC_RECORD_SIZE) ||
        bitmap_offset > file_size ||
        !validate_ranges(data, range_count, glyph_count) ||
        !validate_metrics(data, glyph_count, metrics_offset, bitmap_offset,
                          file_size)) {
        return ESP_ERR_INVALID_SIZE;
    }

    s_font_state.data = data;
    s_font_state.data_size = data_size;
    s_font_state.glyph_count = glyph_count;
    s_font_state.metrics_offset = metrics_offset;
    s_font_state.bitmap_offset = bitmap_offset;
    s_font_state.range_count = range_count;
    s_font_state.ready = true;
    ESP_LOGI(TAG, "[font] ready glyphs=%lu bytes=%lu bpp=%u",
             (unsigned long)glyph_count, (unsigned long)data_size,
             (unsigned)NOTIFIER_FONT_BPP);
    return ESP_OK;
}
