#include "mp3_catalog.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void test_sort_trim_and_optional_files(void)
{
    mp3_song_t songs[4] = {0};
    mp3_catalog_t catalog = {0};
    mp3_catalog_reject_reason_t reason = MP3_CATALOG_REJECT_NONE;

    assert(mp3_catalog_init(&catalog, songs, 4U));
    assert(MP3_CATALOG_ADD_STORED == mp3_catalog_add_candidate(
        &catalog, "/tfcard/mp3", "  002_世界  ", true, true, false, true,
        &reason));
    assert(MP3_CATALOG_ADD_STORED == mp3_catalog_add_candidate(
        &catalog, "/tfcard/mp3", "001_你好", true, true, true, false,
        &reason));

    assert(2U == catalog.count);
    assert(2U == catalog.stats.found_count);
    assert(2U == catalog.stats.valid_count);
    assert(0U == catalog.stats.rejected_count);
    assert(0 == strcmp("001_你好", songs[0].title));
    assert(0 == strcmp("002_世界", songs[1].title));
    assert(songs[0].has_lrc && !songs[0].has_cover);
    assert(!songs[1].has_lrc && songs[1].has_cover);
    assert(0 == strcmp("/tfcard/mp3/001_你好/audio.mp3",
                       songs[0].audio_path));
    assert(0 == strcmp("/tfcard/mp3/001_你好/lyrics.lrc",
                       songs[0].lrc_path));
    assert('\0' == songs[0].cover_path[0]);
}

static void test_rejections(void)
{
    mp3_song_t songs[2] = {0};
    mp3_catalog_t catalog = {0};
    mp3_catalog_reject_reason_t reason = MP3_CATALOG_REJECT_NONE;
    const char invalid_utf8[] = {'x', (char)0xc0, (char)0xaf, '\0'};
    char long_title[MP3_TITLE_MAX_BYTES + 2U] = {0};

    memset(long_title, 'a', sizeof(long_title) - 1U);
    assert(mp3_catalog_init(&catalog, songs, 2U));

    assert(MP3_CATALOG_ADD_REJECTED == mp3_catalog_add_candidate(
        &catalog, "/tfcard/mp3", "file", false, true, false, false,
        &reason));
    assert(MP3_CATALOG_REJECT_NOT_DIRECTORY == reason);
    assert(MP3_CATALOG_ADD_REJECTED == mp3_catalog_add_candidate(
        &catalog, "/tfcard/mp3", "missing", true, false, false, false,
        &reason));
    assert(MP3_CATALOG_REJECT_NO_AUDIO == reason);
    assert(MP3_CATALOG_ADD_REJECTED == mp3_catalog_add_candidate(
        &catalog, "/tfcard/mp3", " \t ", true, true, false, false,
        &reason));
    assert(MP3_CATALOG_REJECT_EMPTY_TITLE == reason);
    assert(MP3_CATALOG_ADD_REJECTED == mp3_catalog_add_candidate(
        &catalog, "/tfcard/mp3", invalid_utf8, true, true, false, false,
        &reason));
    assert(MP3_CATALOG_REJECT_INVALID_NAME == reason);
    assert(MP3_CATALOG_ADD_REJECTED == mp3_catalog_add_candidate(
        &catalog, "/tfcard/mp3", "bad/name", true, true, false, false,
        &reason));
    assert(MP3_CATALOG_REJECT_INVALID_NAME == reason);
    assert(MP3_CATALOG_ADD_REJECTED == mp3_catalog_add_candidate(
        &catalog, "/tfcard/mp3", long_title, true, true, false, false,
        &reason));
    assert(MP3_CATALOG_REJECT_TITLE_TOO_LONG == reason);
    assert(6U == catalog.stats.found_count);
    assert(6U == catalog.stats.rejected_count);
}

static void test_capacity_keeps_lexicographically_first(void)
{
    mp3_song_t songs[MP3_CATALOG_MAX_SONGS] = {0};
    mp3_catalog_t catalog = {0};
    char name[32] = {0};

    assert(mp3_catalog_init(&catalog, songs, MP3_CATALOG_MAX_SONGS));
    for (size_t index = 0U; index < MP3_CATALOG_MAX_SONGS; ++index) {
        snprintf(name, sizeof(name), "song_%03zu", index + 1U);
        assert(MP3_CATALOG_ADD_STORED == mp3_catalog_add_candidate(
            &catalog, "/tfcard/mp3", name, true, true, false, false,
            NULL));
    }
    assert(MP3_CATALOG_ADD_STORED == mp3_catalog_add_candidate(
        &catalog, "/tfcard/mp3", "song_000", true, true, false, false,
        NULL));

    assert(MP3_CATALOG_MAX_SONGS == catalog.count);
    assert(MP3_CATALOG_MAX_SONGS + 1U == catalog.stats.valid_count);
    assert(1U == catalog.stats.truncated_count);
    assert(0 == strcmp("song_000", songs[0].title));
    assert(0 == strcmp("song_127", songs[MP3_CATALOG_MAX_SONGS - 1U].title));
}

static void test_utf8_boundaries(void)
{
    const uint8_t valid[] = "ASCII中文\xf0\x9f\x8e\xb5";
    const uint8_t surrogate[] = {0xed, 0xa0, 0x80};
    const uint8_t too_high[] = {0xf4, 0x90, 0x80, 0x80};
    const uint8_t truncated[] = {0xe4, 0xb8};

    assert(mp3_catalog_is_valid_utf8(valid, sizeof(valid) - 1U));
    assert(!mp3_catalog_is_valid_utf8(surrogate, sizeof(surrogate)));
    assert(!mp3_catalog_is_valid_utf8(too_high, sizeof(too_high)));
    assert(!mp3_catalog_is_valid_utf8(truncated, sizeof(truncated)));
}

int main(void)
{
    test_sort_trim_and_optional_files();
    test_rejections();
    test_capacity_keeps_lexicographically_first();
    test_utf8_boundaries();
    puts("mp3_catalog_test: PASS");
    return 0;
}
