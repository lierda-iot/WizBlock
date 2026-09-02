#include "mp3_lrc.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void *test_alloc(void *context, size_t size)
{
    (void)context;
    return malloc(size);
}

static void test_free(void *context, void *memory)
{
    (void)context;
    free(memory);
}

static const mp3_lrc_allocator_t s_allocator = {
    .alloc = test_alloc,
    .free = test_free,
};

static void test_formats_sort_duplicate_and_lookup(void)
{
    static const uint8_t input[] =
        "\xef\xbb\xbf[00:02.50][00:01.250]第一行\r\n"
        "[0:01.25]后写覆盖\n"
        "[00:00]开始\n"
        "[10:59.999]结束\n";
    mp3_lrc_t lyrics = {0};

    assert(MP3_LRC_OK == mp3_lrc_parse(input, sizeof(input) - 1U,
                                       &s_allocator, &lyrics));
    assert(4U == lyrics.line_count);
    assert(0U == lyrics.lines[0].timestamp_ms);
    assert(1250U == lyrics.lines[1].timestamp_ms);
    assert(2500U == lyrics.lines[2].timestamp_ms);
    assert(659999U == lyrics.lines[3].timestamp_ms);
    assert(0 == strcmp("开始", mp3_lrc_get_text(&lyrics, 0U)));
    assert(0 == strcmp("后写覆盖", mp3_lrc_get_text(&lyrics, 1U)));
    assert(0 == strcmp("第一行", mp3_lrc_get_text(&lyrics, 2U)));
    assert(0U == mp3_lrc_find_line(&lyrics, 0U));
    assert(0U == mp3_lrc_find_line(&lyrics, 1249U));
    assert(1U == mp3_lrc_find_line(&lyrics, 1250U));
    assert(3U == mp3_lrc_find_line(&lyrics, UINT64_MAX));
    mp3_lrc_release(&lyrics);
}

static void test_before_first_and_malformed_lines(void)
{
    static const uint8_t input[] =
        "[ar:artist]\n"
        "[00:60]bad second\n"
        "[1000:00]bad minute\n"
        "[00:01.2]bad fraction\n"
        "[00:05]   \n"
        "plain text\n"
        "[00:10]valid\n";
    mp3_lrc_t lyrics = {0};

    assert(MP3_LRC_OK == mp3_lrc_parse(input, sizeof(input) - 1U,
                                       &s_allocator, &lyrics));
    assert(1U == lyrics.line_count);
    assert(lyrics.malformed_count >= 6U);
    assert(MP3_LRC_INDEX_NONE == mp3_lrc_find_line(&lyrics, 9999U));
    assert(0U == mp3_lrc_find_line(&lyrics, 10000U));
    mp3_lrc_release(&lyrics);
}

static void test_line_capacity_and_file_limit(void)
{
    const size_t record_count = MP3_LRC_MAX_LINES + 1U;
    const size_t bytes_per_record = 16U;
    char *input = calloc(record_count, bytes_per_record);
    mp3_lrc_t lyrics = {0};
    size_t offset = 0U;
    uint8_t one = 0U;

    assert(NULL != input);
    for (size_t index = 0U; index < record_count; ++index) {
        int written = snprintf(input + offset, bytes_per_record,
                               "[%02zu:%02zu]x\n", index / 60U,
                               index % 60U);
        assert(written > 0);
        offset += (size_t)written;
    }
    assert(MP3_LRC_OK == mp3_lrc_parse((const uint8_t *)input, offset,
                                       &s_allocator, &lyrics));
    assert(MP3_LRC_MAX_LINES == lyrics.line_count);
    assert(1U == lyrics.truncated_count);
    mp3_lrc_release(&lyrics);
    free(input);

    assert(MP3_LRC_FILE_TOO_LARGE == mp3_lrc_parse(
        &one, MP3_LRC_MAX_FILE_BYTES + 1U, &s_allocator, &lyrics));
}

static void test_exact_file_limit(void)
{
    uint8_t *input = malloc(MP3_LRC_MAX_FILE_BYTES);
    mp3_lrc_t lyrics = {0};

    assert(NULL != input);
    memset(input, 'x', MP3_LRC_MAX_FILE_BYTES);
    assert(MP3_LRC_OK == mp3_lrc_parse(input, MP3_LRC_MAX_FILE_BYTES,
                                       &s_allocator, &lyrics));
    assert(0U == lyrics.line_count);
    mp3_lrc_release(&lyrics);
    free(input);
}

int main(void)
{
    test_formats_sort_duplicate_and_lookup();
    test_before_first_and_malformed_lines();
    test_line_capacity_and_file_limit();
    test_exact_file_limit();
    puts("mp3_lrc_test: PASS");
    return 0;
}
