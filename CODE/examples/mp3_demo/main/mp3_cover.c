#include "mp3_cover.h"

#include "mp3_file_io.h"

#include "esp_heap_caps.h"
#include "esp_jpeg_dec.h"

#include <stdbool.h>
#include <stddef.h>
#include <string.h>

#define MP3_COVER_JPEG_ALIGNMENT 16U
#define MP3_COVER_BACKGROUND_RGB565 0x2104U

static bool is_start_of_frame(uint8_t marker)
{
    switch (marker) {
    case 0xc0U:
    case 0xc1U:
    case 0xc2U:
    case 0xc3U:
    case 0xc5U:
    case 0xc6U:
    case 0xc7U:
    case 0xc9U:
    case 0xcaU:
    case 0xcbU:
    case 0xcdU:
    case 0xceU:
    case 0xcfU:
        return true;
    default:
        return false;
    }
}

static bool parse_baseline_header(const uint8_t *data, size_t data_size,
                                  uint16_t *width, uint16_t *height)
{
    size_t offset = 2U;

    if (NULL == data || NULL == width || NULL == height || data_size < 4U ||
        0xffU != data[0] || 0xd8U != data[1]) {
        return false;
    }
    while (offset < data_size) {
        while (offset < data_size && 0xffU == data[offset]) {
            ++offset;
        }
        if (offset >= data_size) {
            return false;
        }
        uint8_t marker = data[offset++];

        if (0xd9U == marker || 0xdaU == marker) {
            return false;
        }
        if (0x01U == marker || (marker >= 0xd0U && marker <= 0xd7U)) {
            continue;
        }
        if (offset + 2U > data_size) {
            return false;
        }
        size_t segment_size = ((size_t)data[offset] << 8U) |
                              (size_t)data[offset + 1U];
        if (segment_size < 2U || segment_size > data_size - offset) {
            return false;
        }
        if (is_start_of_frame(marker)) {
            if (0xc0U != marker || segment_size < 8U ||
                8U != data[offset + 2U]) {
                return false;
            }
            uint16_t parsed_height =
                (uint16_t)(((uint16_t)data[offset + 3U] << 8U) |
                           data[offset + 4U]);
            uint16_t parsed_width =
                (uint16_t)(((uint16_t)data[offset + 5U] << 8U) |
                           data[offset + 6U]);
            uint8_t components = data[offset + 7U];

            if (0U == parsed_width || 0U == parsed_height ||
                parsed_width > MP3_COVER_MAX_SOURCE_DIMENSION ||
                parsed_height > MP3_COVER_MAX_SOURCE_DIMENSION ||
                (1U != components && 3U != components)) {
                return false;
            }
            *width = parsed_width;
            *height = parsed_height;
            return true;
        }
        offset += segment_size;
    }
    return false;
}

static void scale_and_center(const uint16_t *source, uint16_t source_width,
                             uint16_t source_height, mp3_cover_t *cover)
{
    uint32_t scaled_width = cover->width;
    uint32_t scaled_height = cover->height;

    if ((uint32_t)source_width * cover->height >
        (uint32_t)source_height * cover->width) {
        scaled_height = ((uint32_t)source_height * cover->width +
                         (source_width / 2U)) /
                        source_width;
    } else {
        scaled_width = ((uint32_t)source_width * cover->height +
                        (source_height / 2U)) /
                       source_height;
    }
    if (0U == scaled_width) {
        scaled_width = 1U;
    }
    if (0U == scaled_height) {
        scaled_height = 1U;
    }

    size_t pixel_count = (size_t)cover->width * cover->height;
    for (size_t index = 0U; index < pixel_count; ++index) {
        cover->pixels[index] = MP3_COVER_BACKGROUND_RGB565;
    }

    uint32_t offset_x = ((uint32_t)cover->width - scaled_width) / 2U;
    uint32_t offset_y = ((uint32_t)cover->height - scaled_height) / 2U;
    for (uint32_t y = 0U; y < scaled_height; ++y) {
        uint32_t source_y = (y * source_height) / scaled_height;
        for (uint32_t x = 0U; x < scaled_width; ++x) {
            uint32_t source_x = (x * source_width) / scaled_width;
            cover->pixels[(offset_y + y) * cover->width + offset_x + x] =
                source[source_y * source_width + source_x];
        }
    }
}

esp_err_t mp3_cover_load(const char *path, mp3_spi_lock_t *spi_lock,
                         uint16_t target_width, uint16_t target_height,
                         mp3_cover_t *cover)
{
    uint8_t *jpeg_data = NULL;
    uint16_t *decoded = NULL;
    size_t jpeg_size = 0U;
    uint16_t source_width = 0U;
    uint16_t source_height = 0U;
    jpeg_dec_handle_t decoder = NULL;
    esp_err_t result = ESP_OK;

    if (NULL == path || NULL == spi_lock || 0U == target_width ||
        0U == target_height || NULL == cover) {
        return ESP_ERR_INVALID_ARG;
    }
    memset(cover, 0, sizeof(*cover));
    result = mp3_file_read_all(path, spi_lock, MP3_COVER_MAX_FILE_BYTES,
                               false, &jpeg_data, &jpeg_size);
    if (ESP_OK != result) {
        return result;
    }
    if (!parse_baseline_header(jpeg_data, jpeg_size, &source_width,
                               &source_height)) {
        result = ESP_ERR_NOT_SUPPORTED;
        goto cleanup;
    }

    jpeg_dec_config_t config = DEFAULT_JPEG_DEC_CONFIG();
    config.output_type = JPEG_PIXEL_FORMAT_RGB565_LE;
    if (JPEG_ERR_OK != jpeg_dec_open(&config, &decoder)) {
        result = ESP_FAIL;
        goto cleanup;
    }
    jpeg_dec_io_t io = {
        .inbuf = jpeg_data,
        .inbuf_len = (int)jpeg_size,
    };
    jpeg_dec_header_info_t header = {0};
    if (JPEG_ERR_OK != jpeg_dec_parse_header(decoder, &io, &header) ||
        header.width != source_width || header.height != source_height) {
        result = ESP_ERR_INVALID_RESPONSE;
        goto cleanup;
    }
    int output_bytes = 0;
    if (JPEG_ERR_OK != jpeg_dec_get_outbuf_len(decoder, &output_bytes) ||
        output_bytes < (int)((size_t)source_width * source_height *
                             sizeof(uint16_t))) {
        result = ESP_ERR_INVALID_SIZE;
        goto cleanup;
    }
    decoded = heap_caps_aligned_alloc(
        MP3_COVER_JPEG_ALIGNMENT, (size_t)output_bytes,
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == decoded) {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    io.outbuf = (uint8_t *)decoded;
    if (JPEG_ERR_OK != jpeg_dec_process(decoder, &io)) {
        result = ESP_FAIL;
        goto cleanup;
    }

    cover->pixels = heap_caps_malloc(
        (size_t)target_width * target_height * sizeof(uint16_t),
        MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    if (NULL == cover->pixels) {
        result = ESP_ERR_NO_MEM;
        goto cleanup;
    }
    cover->width = target_width;
    cover->height = target_height;
    cover->source_width = source_width;
    cover->source_height = source_height;
    scale_and_center(decoded, source_width, source_height, cover);

cleanup:
    if (NULL != decoder) {
        (void)jpeg_dec_close(decoder);
    }
    heap_caps_free(decoded);
    heap_caps_free(jpeg_data);
    if (ESP_OK != result) {
        mp3_cover_release(cover);
    }
    return result;
}

void mp3_cover_release(mp3_cover_t *cover)
{
    if (NULL == cover) {
        return;
    }
    heap_caps_free(cover->pixels);
    memset(cover, 0, sizeof(*cover));
}
