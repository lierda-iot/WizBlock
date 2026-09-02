#include "mp3_catalog.h"

#include <stdio.h>
#include <string.h>

static bool is_continuation(uint8_t value)
{
    return (value & 0xc0U) == 0x80U;
}

bool mp3_catalog_is_valid_utf8(const uint8_t *text, size_t length)
{
    size_t index = 0U;

    if (NULL == text && 0U != length) {
        return false;
    }
    while (index < length) {
        uint8_t first = text[index];

        if (first <= 0x7fU) {
            ++index;
            continue;
        }
        if (first >= 0xc2U && first <= 0xdfU) {
            if (index + 1U >= length || !is_continuation(text[index + 1U])) {
                return false;
            }
            index += 2U;
            continue;
        }
        if (first >= 0xe0U && first <= 0xefU) {
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
            continue;
        }
        if (first >= 0xf0U && first <= 0xf4U) {
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
            continue;
        }
        return false;
    }
    return true;
}

static mp3_catalog_add_result_t reject_candidate(
    mp3_catalog_t *catalog, mp3_catalog_reject_reason_t reason,
    mp3_catalog_reject_reason_t *reject_reason)
{
    if (NULL != catalog) {
        ++catalog->stats.rejected_count;
    }
    if (NULL != reject_reason) {
        *reject_reason = reason;
    }
    return MP3_CATALOG_ADD_REJECTED;
}

static bool contains_invalid_title_byte(const char *title, size_t length)
{
    if (NULL == title) {
        return true;
    }
    for (size_t index = 0U; index < length; ++index) {
        uint8_t value = (uint8_t)title[index];

        if (value < 0x20U || 0x7fU == value) {
            return true;
        }
    }
    return false;
}

static bool make_path(char *destination, size_t destination_size,
                      const char *root_path, const char *directory_name,
                      const char *file_name)
{
    int written = 0;

    if (NULL == destination || 0U == destination_size || NULL == root_path ||
        NULL == directory_name || NULL == file_name) {
        return false;
    }
    written = snprintf(destination, destination_size, "%s/%s/%s", root_path,
                       directory_name, file_name);
    return written >= 0 && (size_t)written < destination_size;
}

bool mp3_catalog_init(mp3_catalog_t *catalog, mp3_song_t *storage,
                      size_t capacity)
{
    if (NULL == catalog || NULL == storage || 0U == capacity ||
        capacity > MP3_CATALOG_MAX_SONGS) {
        return false;
    }
    memset(catalog, 0, sizeof(*catalog));
    memset(storage, 0, capacity * sizeof(*storage));
    catalog->songs = storage;
    catalog->capacity = capacity;
    return true;
}

mp3_catalog_add_result_t mp3_catalog_add_candidate(
    mp3_catalog_t *catalog, const char *root_path, const char *directory_name,
    bool is_directory, bool has_audio, bool has_lrc, bool has_cover,
    mp3_catalog_reject_reason_t *reject_reason)
{
    size_t name_length = 0U;
    size_t title_start = 0U;
    size_t title_end = 0U;
    size_t title_length = 0U;
    size_t insert_at = 0U;
    mp3_song_t candidate = {0};

    if (NULL != reject_reason) {
        *reject_reason = MP3_CATALOG_REJECT_NONE;
    }
    if (NULL == catalog || NULL == catalog->songs || 0U == catalog->capacity) {
        if (NULL != reject_reason) {
            *reject_reason = MP3_CATALOG_REJECT_INVALID_NAME;
        }
        return MP3_CATALOG_ADD_REJECTED;
    }
    ++catalog->stats.found_count;
    if (!is_directory) {
        return reject_candidate(catalog, MP3_CATALOG_REJECT_NOT_DIRECTORY,
                                reject_reason);
    }
    if (!has_audio) {
        return reject_candidate(catalog, MP3_CATALOG_REJECT_NO_AUDIO,
                                reject_reason);
    }
    if (NULL == root_path || '\0' == root_path[0] ||
        NULL == directory_name) {
        return reject_candidate(catalog, MP3_CATALOG_REJECT_INVALID_NAME,
                                reject_reason);
    }

    name_length = strlen(directory_name);
    if (0U == name_length || 0 == strcmp(directory_name, ".") ||
        0 == strcmp(directory_name, "..") ||
        NULL != strchr(directory_name, '/') ||
        !mp3_catalog_is_valid_utf8((const uint8_t *)directory_name,
                                   name_length)) {
        return reject_candidate(catalog, MP3_CATALOG_REJECT_INVALID_NAME,
                                reject_reason);
    }

    title_start = 0U;
    while (title_start < name_length &&
           (' ' == directory_name[title_start] ||
            '\t' == directory_name[title_start])) {
        ++title_start;
    }
    title_end = name_length;
    while (title_end > title_start &&
           (' ' == directory_name[title_end - 1U] ||
            '\t' == directory_name[title_end - 1U])) {
        --title_end;
    }
    title_length = title_end - title_start;
    if (0U == title_length) {
        return reject_candidate(catalog, MP3_CATALOG_REJECT_EMPTY_TITLE,
                                reject_reason);
    }
    if (title_length > MP3_TITLE_MAX_BYTES) {
        return reject_candidate(catalog, MP3_CATALOG_REJECT_TITLE_TOO_LONG,
                                reject_reason);
    }
    if (contains_invalid_title_byte(directory_name + title_start,
                                    title_length)) {
        return reject_candidate(catalog, MP3_CATALOG_REJECT_INVALID_NAME,
                                reject_reason);
    }

    memcpy(candidate.title, directory_name + title_start, title_length);
    candidate.title[title_length] = '\0';
    if (!make_path(candidate.audio_path, sizeof(candidate.audio_path),
                   root_path, directory_name, "audio.mp3") ||
        (has_lrc &&
         !make_path(candidate.lrc_path, sizeof(candidate.lrc_path), root_path,
                    directory_name, "lyrics.lrc")) ||
        (has_cover &&
         !make_path(candidate.cover_path, sizeof(candidate.cover_path),
                    root_path, directory_name, "cover.jpg"))) {
        return reject_candidate(catalog, MP3_CATALOG_REJECT_PATH_TOO_LONG,
                                reject_reason);
    }
    candidate.has_lrc = has_lrc;
    candidate.has_cover = has_cover;

    for (insert_at = 0U; insert_at < catalog->count; ++insert_at) {
        int comparison = strcmp(candidate.title,
                                catalog->songs[insert_at].title);

        if (0 == comparison) {
            comparison = strcmp(candidate.audio_path,
                                catalog->songs[insert_at].audio_path);
        }

        if (0 == comparison) {
            return reject_candidate(catalog, MP3_CATALOG_REJECT_DUPLICATE,
                                    reject_reason);
        }
        if (comparison < 0) {
            break;
        }
    }

    ++catalog->stats.valid_count;
    if (catalog->count < catalog->capacity) {
        memmove(&catalog->songs[insert_at + 1U],
                &catalog->songs[insert_at],
                (catalog->count - insert_at) * sizeof(candidate));
        catalog->songs[insert_at] = candidate;
        ++catalog->count;
        catalog->stats.truncated_count =
            catalog->stats.valid_count - catalog->count;
        return MP3_CATALOG_ADD_STORED;
    }

    catalog->stats.truncated_count =
        catalog->stats.valid_count - catalog->count;
    if (insert_at >= catalog->capacity) {
        return MP3_CATALOG_ADD_TRUNCATED;
    }
    memmove(&catalog->songs[insert_at + 1U],
            &catalog->songs[insert_at],
            (catalog->capacity - insert_at - 1U) * sizeof(candidate));
    catalog->songs[insert_at] = candidate;
    return MP3_CATALOG_ADD_STORED;
}
