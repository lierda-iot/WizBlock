#include "mp3_lrc.h"

#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    uint32_t timestamp_ms;
    uint32_t text_offset;
    uint32_t text_length;
} lrc_temp_line_t;

static bool is_continuation(uint8_t value)
{
    return (value & 0xc0U) == 0x80U;
}

static bool is_valid_utf8(const uint8_t *text, size_t length)
{
    size_t index = 0U;

    if (NULL == text && 0U != length) {
        return false;
    }
    while (index < length) {
        uint8_t first = text[index];

        if (first <= 0x7fU) {
            if (0U == first) {
                return false;
            }
            ++index;
        } else if (first >= 0xc2U && first <= 0xdfU) {
            if (index + 1U >= length || !is_continuation(text[index + 1U])) {
                return false;
            }
            index += 2U;
        } else if (first >= 0xe0U && first <= 0xefU) {
            uint8_t second = 0U;

            if (index + 2U >= length) {
                return false;
            }
            second = text[index + 1U];
            if (!is_continuation(text[index + 2U]) ||
                (0xe0U == first && (second < 0xa0U || second > 0xbfU)) ||
                (0xedU == first && (second < 0x80U || second > 0x9fU)) ||
                ((0xe0U != first && 0xedU != first) &&
                 !is_continuation(second))) {
                return false;
            }
            index += 3U;
        } else if (first >= 0xf0U && first <= 0xf4U) {
            uint8_t second = 0U;

            if (index + 3U >= length) {
                return false;
            }
            second = text[index + 1U];
            if (!is_continuation(text[index + 2U]) ||
                !is_continuation(text[index + 3U]) ||
                (0xf0U == first && (second < 0x90U || second > 0xbfU)) ||
                (0xf4U == first && (second < 0x80U || second > 0x8fU)) ||
                ((first > 0xf0U && first < 0xf4U) &&
                 !is_continuation(second))) {
                return false;
            }
            index += 4U;
        } else {
            return false;
        }
    }
    return true;
}

static bool parse_time_tag(const char *text, size_t limit, size_t start,
                           uint32_t *timestamp_ms, size_t *next)
{
    size_t cursor = start;
    uint32_t minutes = 0U;
    uint32_t seconds = 0U;
    uint32_t fraction_ms = 0U;
    size_t minute_digits = 0U;

    if (NULL == text || NULL == timestamp_ms || NULL == next ||
        cursor >= limit || '[' != text[cursor]) {
        return false;
    }
    ++cursor;
    while (cursor < limit && text[cursor] >= '0' && text[cursor] <= '9' &&
           minute_digits < 3U) {
        minutes = (minutes * 10U) + (uint32_t)(text[cursor] - '0');
        ++cursor;
        ++minute_digits;
    }
    if (0U == minute_digits || cursor >= limit || ':' != text[cursor]) {
        return false;
    }
    ++cursor;
    if (cursor + 1U >= limit || text[cursor] < '0' || text[cursor] > '9' ||
        text[cursor + 1U] < '0' || text[cursor + 1U] > '9') {
        return false;
    }
    seconds = (uint32_t)(text[cursor] - '0') * 10U +
              (uint32_t)(text[cursor + 1U] - '0');
    cursor += 2U;
    if (seconds > 59U) {
        return false;
    }
    if (cursor < limit && '.' == text[cursor]) {
        size_t fraction_digits = 0U;
        uint32_t fraction = 0U;

        ++cursor;
        while (cursor < limit && text[cursor] >= '0' &&
               text[cursor] <= '9' && fraction_digits < 3U) {
            fraction = (fraction * 10U) +
                       (uint32_t)(text[cursor] - '0');
            ++cursor;
            ++fraction_digits;
        }
        if (2U != fraction_digits && 3U != fraction_digits) {
            return false;
        }
        fraction_ms = (2U == fraction_digits) ? fraction * 10U : fraction;
    }
    if (cursor >= limit || ']' != text[cursor]) {
        return false;
    }
    ++cursor;
    *timestamp_ms = ((minutes * 60U) + seconds) * 1000U + fraction_ms;
    *next = cursor;
    return true;
}

static void add_temp_line(lrc_temp_line_t *lines, size_t *line_count,
                          size_t *truncated_count, uint32_t timestamp_ms,
                          uint32_t text_offset, uint32_t text_length)
{
    if (NULL == lines || NULL == line_count || NULL == truncated_count) {
        return;
    }
    for (size_t index = 0U; index < *line_count; ++index) {
        if (timestamp_ms == lines[index].timestamp_ms) {
            lines[index].text_offset = text_offset;
            lines[index].text_length = text_length;
            return;
        }
    }
    if (*line_count >= MP3_LRC_MAX_LINES) {
        ++(*truncated_count);
        return;
    }
    lines[*line_count].timestamp_ms = timestamp_ms;
    lines[*line_count].text_offset = text_offset;
    lines[*line_count].text_length = text_length;
    ++(*line_count);
}

static int compare_temp_line(const void *left, const void *right)
{
    const lrc_temp_line_t *left_line = left;
    const lrc_temp_line_t *right_line = right;

    if (left_line->timestamp_ms < right_line->timestamp_ms) {
        return -1;
    }
    if (left_line->timestamp_ms > right_line->timestamp_ms) {
        return 1;
    }
    return 0;
}

static void release_parts(mp3_lrc_t *lyrics)
{
    if (NULL == lyrics || NULL == lyrics->allocator.free) {
        return;
    }
    if (NULL != lyrics->lines) {
        lyrics->allocator.free(lyrics->allocator.context, lyrics->lines);
    }
    if (NULL != lyrics->text_pool) {
        lyrics->allocator.free(lyrics->allocator.context, lyrics->text_pool);
    }
    lyrics->lines = NULL;
    lyrics->text_pool = NULL;
}

mp3_lrc_result_t mp3_lrc_parse(const uint8_t *data, size_t data_size,
                               const mp3_lrc_allocator_t *allocator,
                               mp3_lrc_t *lyrics)
{
    lrc_temp_line_t *temp_lines = NULL;
    size_t temp_count = 0U;
    size_t cursor = 0U;

    if (NULL == lyrics) {
        return MP3_LRC_INVALID_ARGUMENT;
    }
    memset(lyrics, 0, sizeof(*lyrics));
    if ((NULL == data && 0U != data_size) || NULL == allocator ||
        NULL == allocator->alloc || NULL == allocator->free) {
        return MP3_LRC_INVALID_ARGUMENT;
    }
    if (data_size > MP3_LRC_MAX_FILE_BYTES) {
        return MP3_LRC_FILE_TOO_LARGE;
    }
    lyrics->allocator = *allocator;
    lyrics->text_pool = allocator->alloc(allocator->context, data_size + 1U);
    temp_lines = allocator->alloc(allocator->context,
                                  MP3_LRC_MAX_LINES * sizeof(*temp_lines));
    if (NULL == lyrics->text_pool || NULL == temp_lines) {
        if (NULL != temp_lines) {
            allocator->free(allocator->context, temp_lines);
        }
        release_parts(lyrics);
        memset(lyrics, 0, sizeof(*lyrics));
        return MP3_LRC_NO_MEMORY;
    }
    if (data_size > 0U) {
        memcpy(lyrics->text_pool, data, data_size);
    }
    lyrics->text_pool[data_size] = '\0';
    lyrics->text_pool_size = data_size + 1U;

    while (cursor < data_size) {
        size_t line_start = cursor;
        size_t line_end = cursor;
        size_t tag_start = 0U;
        size_t tag_cursor = 0U;
        size_t text_start = 0U;
        size_t text_end = 0U;
        size_t tag_count = 0U;
        bool valid_line = true;

        while (line_end < data_size && '\r' != lyrics->text_pool[line_end] &&
               '\n' != lyrics->text_pool[line_end]) {
            ++line_end;
        }
        cursor = line_end;
        while (cursor < data_size &&
               ('\r' == lyrics->text_pool[cursor] ||
                '\n' == lyrics->text_pool[cursor])) {
            lyrics->text_pool[cursor] = '\0';
            ++cursor;
        }
        lyrics->text_pool[line_end] = '\0';

        tag_start = line_start;
        if (0U == line_start && line_end >= 3U &&
            0xefU == (uint8_t)lyrics->text_pool[0] &&
            0xbbU == (uint8_t)lyrics->text_pool[1] &&
            0xbfU == (uint8_t)lyrics->text_pool[2]) {
            tag_start = 3U;
        }
        tag_cursor = tag_start;
        while (tag_cursor < line_end && '[' == lyrics->text_pool[tag_cursor]) {
            uint32_t timestamp_ms = 0U;
            size_t next = 0U;

            if (!parse_time_tag(lyrics->text_pool, line_end, tag_cursor,
                                &timestamp_ms, &next)) {
                valid_line = false;
                break;
            }
            tag_cursor = next;
            ++tag_count;
        }
        if (!valid_line || 0U == tag_count) {
            ++lyrics->malformed_count;
            continue;
        }

        text_start = tag_cursor;
        while (text_start < line_end &&
               (' ' == lyrics->text_pool[text_start] ||
                '\t' == lyrics->text_pool[text_start])) {
            ++text_start;
        }
        text_end = line_end;
        while (text_end > text_start &&
               (' ' == lyrics->text_pool[text_end - 1U] ||
                '\t' == lyrics->text_pool[text_end - 1U])) {
            --text_end;
        }
        if (text_start == text_end ||
            !is_valid_utf8((const uint8_t *)lyrics->text_pool + text_start,
                           text_end - text_start)) {
            ++lyrics->malformed_count;
            continue;
        }
        lyrics->text_pool[text_end] = '\0';

        tag_cursor = tag_start;
        while (tag_cursor < text_start && '[' == lyrics->text_pool[tag_cursor]) {
            uint32_t timestamp_ms = 0U;
            size_t next = 0U;

            if (!parse_time_tag(lyrics->text_pool, line_end, tag_cursor,
                                &timestamp_ms, &next)) {
                break;
            }
            add_temp_line(temp_lines, &temp_count, &lyrics->truncated_count,
                          timestamp_ms, (uint32_t)text_start,
                          (uint32_t)(text_end - text_start));
            tag_cursor = next;
        }
    }

    if (temp_count > 0U) {
        qsort(temp_lines, temp_count, sizeof(*temp_lines), compare_temp_line);
        lyrics->lines = allocator->alloc(allocator->context,
                                          temp_count * sizeof(*lyrics->lines));
        if (NULL == lyrics->lines) {
            allocator->free(allocator->context, temp_lines);
            release_parts(lyrics);
            memset(lyrics, 0, sizeof(*lyrics));
            return MP3_LRC_NO_MEMORY;
        }
        for (size_t index = 0U; index < temp_count; ++index) {
            lyrics->lines[index].timestamp_ms = temp_lines[index].timestamp_ms;
            lyrics->lines[index].text_offset = temp_lines[index].text_offset;
            lyrics->lines[index].text_length = temp_lines[index].text_length;
        }
        lyrics->line_count = temp_count;
    }
    allocator->free(allocator->context, temp_lines);
    return MP3_LRC_OK;
}

void mp3_lrc_release(mp3_lrc_t *lyrics)
{
    if (NULL == lyrics) {
        return;
    }
    release_parts(lyrics);
    memset(lyrics, 0, sizeof(*lyrics));
}

size_t mp3_lrc_find_line(const mp3_lrc_t *lyrics, uint64_t position_ms)
{
    size_t low = 0U;
    size_t high = 0U;

    if (NULL == lyrics || NULL == lyrics->lines || 0U == lyrics->line_count ||
        position_ms < lyrics->lines[0].timestamp_ms) {
        return MP3_LRC_INDEX_NONE;
    }
    high = lyrics->line_count;
    while (low < high) {
        size_t middle = low + ((high - low) / 2U);

        if ((uint64_t)lyrics->lines[middle].timestamp_ms <= position_ms) {
            low = middle + 1U;
        } else {
            high = middle;
        }
    }
    return low - 1U;
}

const char *mp3_lrc_get_text(const mp3_lrc_t *lyrics, size_t line_index)
{
    uint32_t offset = 0U;

    if (NULL == lyrics || NULL == lyrics->text_pool ||
        NULL == lyrics->lines || line_index >= lyrics->line_count) {
        return NULL;
    }
    offset = lyrics->lines[line_index].text_offset;
    if ((size_t)offset >= lyrics->text_pool_size) {
        return NULL;
    }
    return lyrics->text_pool + offset;
}
