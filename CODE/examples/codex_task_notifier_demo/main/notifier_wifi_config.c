#include "notifier_wifi_config.h"

#include <stdint.h>
#include <string.h>

static bool is_utf8_continuation(uint8_t byte)
{
    return 0x80U == (byte & 0xC0U);
}

static bool wifi_ssid_is_displayable(const char *ssid, size_t *length)
{
    const uint8_t *bytes = (const uint8_t *)ssid;
    size_t ssid_length = 0U;
    size_t index = 0U;

    if (NULL == ssid || NULL == length) {
        return false;
    }
    ssid_length = strlen(ssid);
    if (0U == ssid_length || NOTIFIER_WIFI_SSID_MAX_BYTES < ssid_length) {
        return false;
    }

    while (index < ssid_length) {
        uint8_t first = bytes[index];

        if (0x80U > first) {
            if (0x20U > first || 0x7FU == first) {
                return false;
            }
            index++;
        } else if (0xC2U <= first && 0xDFU >= first) {
            if (index + 1U >= ssid_length ||
                !is_utf8_continuation(bytes[index + 1U])) {
                return false;
            }
            index += 2U;
        } else if (0xE0U == first) {
            if (index + 2U >= ssid_length ||
                0xA0U > bytes[index + 1U] ||
                0xBFU < bytes[index + 1U] ||
                !is_utf8_continuation(bytes[index + 2U])) {
                return false;
            }
            index += 3U;
        } else if ((0xE1U <= first && 0xECU >= first) ||
                   (0xEEU <= first && 0xEFU >= first)) {
            if (index + 2U >= ssid_length ||
                !is_utf8_continuation(bytes[index + 1U]) ||
                !is_utf8_continuation(bytes[index + 2U])) {
                return false;
            }
            index += 3U;
        } else if (0xEDU == first) {
            if (index + 2U >= ssid_length ||
                0x80U > bytes[index + 1U] ||
                0x9FU < bytes[index + 1U] ||
                !is_utf8_continuation(bytes[index + 2U])) {
                return false;
            }
            index += 3U;
        } else if (0xF0U == first) {
            if (index + 3U >= ssid_length ||
                0x90U > bytes[index + 1U] ||
                0xBFU < bytes[index + 1U] ||
                !is_utf8_continuation(bytes[index + 2U]) ||
                !is_utf8_continuation(bytes[index + 3U])) {
                return false;
            }
            index += 4U;
        } else if (0xF1U <= first && 0xF3U >= first) {
            if (index + 3U >= ssid_length ||
                !is_utf8_continuation(bytes[index + 1U]) ||
                !is_utf8_continuation(bytes[index + 2U]) ||
                !is_utf8_continuation(bytes[index + 3U])) {
                return false;
            }
            index += 4U;
        } else if (0xF4U == first) {
            if (index + 3U >= ssid_length ||
                0x80U > bytes[index + 1U] ||
                0x8FU < bytes[index + 1U] ||
                !is_utf8_continuation(bytes[index + 2U]) ||
                !is_utf8_continuation(bytes[index + 3U])) {
                return false;
            }
            index += 4U;
        } else {
            return false;
        }
    }
    *length = ssid_length;
    return true;
}

static bool scan_network_precedes(const notifier_wifi_scan_network_t *left,
                                  const notifier_wifi_scan_network_t *right)
{
    if (left->rssi != right->rssi) {
        return left->rssi > right->rssi;
    }
    return 0 > strcmp(left->ssid, right->ssid);
}

static void set_scan_network(notifier_wifi_scan_network_t *network,
                             const char *ssid, size_t ssid_length,
                             int8_t rssi)
{
    memset(network, 0, sizeof(*network));
    memcpy(network->ssid, ssid, ssid_length);
    network->rssi = rssi;
}

static void sort_scan_networks(notifier_wifi_scan_list_t *list)
{
    size_t index = 0U;

    for (index = 1U; index < list->count; index++) {
        notifier_wifi_scan_network_t network = list->networks[index];
        size_t destination = index;

        while (0U < destination &&
               scan_network_precedes(&network,
                                     &list->networks[destination - 1U])) {
            list->networks[destination] =
                list->networks[destination - 1U];
            destination--;
        }
        list->networks[destination] = network;
    }
}

notifier_wifi_config_result_t notifier_wifi_config_validate(
    const char *ssid, const char *password)
{
    const char *effective_password = (NULL != password) ? password : "";
    size_t ssid_length = 0U;
    size_t password_length = 0U;

    if (NULL == ssid || '\0' == ssid[0]) {
        return NOTIFIER_WIFI_CONFIG_SSID_REQUIRED;
    }
    ssid_length = strlen(ssid);
    if (NOTIFIER_WIFI_SSID_MAX_BYTES < ssid_length) {
        return NOTIFIER_WIFI_CONFIG_SSID_TOO_LONG;
    }

    password_length = strlen(effective_password);
    if (0U != password_length &&
        (password_length < NOTIFIER_WIFI_PASSWORD_MIN_BYTES ||
         NOTIFIER_WIFI_PASSWORD_MAX_BYTES < password_length)) {
        return NOTIFIER_WIFI_CONFIG_PASSWORD_LENGTH;
    }
    for (size_t index = 0U; index < password_length; ++index) {
        uint8_t byte = (uint8_t)effective_password[index];

        if (byte < 0x20U || 0x7EU < byte) {
            return NOTIFIER_WIFI_CONFIG_PASSWORD_CHARACTER;
        }
    }
    return NOTIFIER_WIFI_CONFIG_OK;
}

notifier_wifi_config_result_t notifier_wifi_credentials_set(
    notifier_wifi_credentials_t *credentials, const char *ssid,
    const char *password)
{
    const char *effective_password = (NULL != password) ? password : "";
    notifier_wifi_config_result_t result = NOTIFIER_WIFI_CONFIG_OK;
    size_t ssid_length = 0U;
    size_t password_length = 0U;

    if (NULL == credentials) {
        return NOTIFIER_WIFI_CONFIG_INVALID_ARGUMENT;
    }
    memset(credentials, 0, sizeof(*credentials));
    result = notifier_wifi_config_validate(ssid, effective_password);
    if (NOTIFIER_WIFI_CONFIG_OK != result) {
        return result;
    }

    ssid_length = strlen(ssid);
    password_length = strlen(effective_password);
    memcpy(credentials->ssid, ssid, ssid_length);
    memcpy(credentials->password, effective_password, password_length);
    return NOTIFIER_WIFI_CONFIG_OK;
}

void notifier_wifi_scan_list_init(notifier_wifi_scan_list_t *list)
{
    if (NULL != list) {
        memset(list, 0, sizeof(*list));
    }
}

bool notifier_wifi_scan_list_add(notifier_wifi_scan_list_t *list,
                                 const char *ssid, int8_t rssi)
{
    notifier_wifi_scan_network_t candidate = {0};
    size_t ssid_length = 0U;
    size_t index = 0U;
    size_t weakest_index = 0U;

    if (NULL == list ||
        !wifi_ssid_is_displayable(ssid, &ssid_length)) {
        return false;
    }
    if (NOTIFIER_WIFI_SCAN_MAX_NETWORKS < list->count) {
        list->count = NOTIFIER_WIFI_SCAN_MAX_NETWORKS;
    }
    for (index = 0U; index < list->count; index++) {
        if (0 == strcmp(list->networks[index].ssid, ssid)) {
            if (rssi > list->networks[index].rssi) {
                list->networks[index].rssi = rssi;
            }
            return true;
        }
    }

    set_scan_network(&candidate, ssid, ssid_length, rssi);
    if (NOTIFIER_WIFI_SCAN_MAX_NETWORKS > list->count) {
        list->networks[list->count] = candidate;
        list->count++;
        return true;
    }

    for (index = 1U; index < list->count; index++) {
        if (scan_network_precedes(&list->networks[weakest_index],
                                  &list->networks[index])) {
            weakest_index = index;
        }
    }
    if (scan_network_precedes(&candidate,
                              &list->networks[weakest_index])) {
        list->networks[weakest_index] = candidate;
    }
    return true;
}

void notifier_wifi_scan_list_finalize(notifier_wifi_scan_list_t *list,
                                      const char *saved_ssid)
{
    size_t saved_length = 0U;
    size_t index = 0U;

    if (NULL == list) {
        return;
    }
    if (NOTIFIER_WIFI_SCAN_MAX_NETWORKS < list->count) {
        list->count = NOTIFIER_WIFI_SCAN_MAX_NETWORKS;
    }
    sort_scan_networks(list);
    list->selected_index = 0U;
    if (!wifi_ssid_is_displayable(saved_ssid, &saved_length)) {
        return;
    }
    for (index = 0U; index < list->count; index++) {
        if (0 == strcmp(list->networks[index].ssid, saved_ssid)) {
            list->selected_index = index;
            return;
        }
    }

    if (NOTIFIER_WIFI_SCAN_MAX_NETWORKS == list->count) {
        list->count--;
    }
    if (0U < list->count) {
        memmove(&list->networks[1], &list->networks[0],
                list->count * sizeof(list->networks[0]));
    }
    set_scan_network(&list->networks[0], saved_ssid, saved_length,
                     INT8_MIN);
    list->count++;
}
