#include "network_manager_wifi_scan.h"

#include <string.h>

static bool is_utf8_continuation(uint8_t byte)
{
    return 0x80U == (byte & 0xC0U);
}

static bool ssid_is_displayable(const uint8_t *ssid, size_t length)
{
    size_t index = 0U;

    while (index < length) {
        const uint8_t first = ssid[index];
        if (first < 0x80U) {
            if (first < 0x20U || 0x7FU == first) return false;
            ++index;
        } else if (first >= 0xC2U && first <= 0xDFU) {
            if (index + 1U >= length ||
                !is_utf8_continuation(ssid[index + 1U])) return false;
            index += 2U;
        } else if (0xE0U == first) {
            if (index + 2U >= length || ssid[index + 1U] < 0xA0U ||
                ssid[index + 1U] > 0xBFU ||
                !is_utf8_continuation(ssid[index + 2U])) return false;
            index += 3U;
        } else if ((first >= 0xE1U && first <= 0xECU) ||
                   (first >= 0xEEU && first <= 0xEFU)) {
            if (index + 2U >= length ||
                !is_utf8_continuation(ssid[index + 1U]) ||
                !is_utf8_continuation(ssid[index + 2U])) return false;
            index += 3U;
        } else if (0xEDU == first) {
            if (index + 2U >= length || ssid[index + 1U] < 0x80U ||
                ssid[index + 1U] > 0x9FU ||
                !is_utf8_continuation(ssid[index + 2U])) return false;
            index += 3U;
        } else if (0xF0U == first) {
            if (index + 3U >= length || ssid[index + 1U] < 0x90U ||
                ssid[index + 1U] > 0xBFU ||
                !is_utf8_continuation(ssid[index + 2U]) ||
                !is_utf8_continuation(ssid[index + 3U])) return false;
            index += 4U;
        } else if (first >= 0xF1U && first <= 0xF3U) {
            if (index + 3U >= length ||
                !is_utf8_continuation(ssid[index + 1U]) ||
                !is_utf8_continuation(ssid[index + 2U]) ||
                !is_utf8_continuation(ssid[index + 3U])) return false;
            index += 4U;
        } else if (0xF4U == first) {
            if (index + 3U >= length || ssid[index + 1U] < 0x80U ||
                ssid[index + 1U] > 0x8FU ||
                !is_utf8_continuation(ssid[index + 2U]) ||
                !is_utf8_continuation(ssid[index + 3U])) return false;
            index += 4U;
        } else {
            return false;
        }
    }
    return true;
}

static int compare_ssid(const network_manager_wifi_scan_entry_t *left,
                        const network_manager_wifi_scan_entry_t *right)
{
    const size_t shared_length = (left->ssid_len < right->ssid_len) ?
                                 left->ssid_len : right->ssid_len;
    const int compared = memcmp(left->ssid, right->ssid, shared_length);

    if (0 != compared) {
        return compared;
    }
    if (left->ssid_len == right->ssid_len) {
        return 0;
    }
    return (left->ssid_len < right->ssid_len) ? -1 : 1;
}

static bool entry_precedes(const network_manager_wifi_scan_entry_t *left,
                           const network_manager_wifi_scan_entry_t *right)
{
    if (left->rssi != right->rssi) {
        return left->rssi > right->rssi;
    }
    return 0 > compare_ssid(left, right);
}

void network_manager_wifi_scan_list_init(
    network_manager_wifi_scan_list_t *list)
{
    if (NULL != list) {
        *list = (network_manager_wifi_scan_list_t){0};
    }
}

bool network_manager_wifi_scan_list_add(
    network_manager_wifi_scan_list_t *list,
    const uint8_t *ssid,
    size_t ssid_len,
    int8_t rssi,
    bool secure)
{
    network_manager_wifi_scan_entry_t candidate = {0};
    size_t weakest_index = 0U;

    if (NULL == list || NULL == ssid || 0U == ssid_len ||
        NETWORK_MANAGER_WIFI_SSID_MAX_BYTES < ssid_len || !secure ||
        !ssid_is_displayable(ssid, ssid_len)) {
        return false;
    }
    if (NETWORK_MANAGER_WIFI_SCAN_MAX_RESULTS < list->count) {
        list->count = NETWORK_MANAGER_WIFI_SCAN_MAX_RESULTS;
    }
    for (size_t index = 0U; index < list->count; ++index) {
        network_manager_wifi_scan_entry_t *entry = &list->entries[index];
        if (ssid_len == entry->ssid_len &&
            0 == memcmp(ssid, entry->ssid, ssid_len)) {
            if (rssi > entry->rssi) {
                entry->rssi = rssi;
                entry->secure = secure;
            } else if (rssi == entry->rssi) {
                entry->secure = entry->secure || secure;
            }
            return true;
        }
    }

    memcpy(candidate.ssid, ssid, ssid_len);
    candidate.ssid_len = (uint8_t)ssid_len;
    candidate.rssi = rssi;
    candidate.secure = secure;
    if (list->count < NETWORK_MANAGER_WIFI_SCAN_MAX_RESULTS) {
        list->entries[list->count++] = candidate;
        return true;
    }
    for (size_t index = 1U; index < list->count; ++index) {
        if (entry_precedes(&list->entries[weakest_index],
                           &list->entries[index])) {
            weakest_index = index;
        }
    }
    if (entry_precedes(&candidate, &list->entries[weakest_index])) {
        list->entries[weakest_index] = candidate;
    }
    return true;
}

void network_manager_wifi_scan_list_finalize(
    network_manager_wifi_scan_list_t *list)
{
    if (NULL == list) {
        return;
    }
    for (size_t index = 1U; index < list->count; ++index) {
        const network_manager_wifi_scan_entry_t entry = list->entries[index];
        size_t destination = index;

        while (0U < destination &&
               entry_precedes(&entry, &list->entries[destination - 1U])) {
            list->entries[destination] = list->entries[destination - 1U];
            --destination;
        }
        list->entries[destination] = entry;
    }
}
