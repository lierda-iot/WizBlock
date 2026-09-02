#include "network_manager_wifi_scan.h"

#include <assert.h>
#include <string.h>

static void add_name(network_manager_wifi_scan_list_t *list,
                     const char *ssid, int8_t rssi, bool secure)
{
    size_t length = 0U;

    while ('\0' != ssid[length]) {
        ++length;
    }
    assert(network_manager_wifi_scan_list_add(
        list, (const uint8_t *)ssid, length, rssi, secure));
}

int main(void)
{
    network_manager_wifi_scan_list_t list = {0};

    network_manager_wifi_scan_list_init(&list);
    assert(!network_manager_wifi_scan_list_add(
        NULL, (const uint8_t *)"a", 1U, -10, true));
    assert(!network_manager_wifi_scan_list_add(
        &list, NULL, 1U, -10, true));
    assert(!network_manager_wifi_scan_list_add(
        &list, (const uint8_t *)"", 0U, -10, true));
    assert(!network_manager_wifi_scan_list_add(
        &list, (const uint8_t *)"012345678901234567890123456789012",
        33U, -10, true));

    add_name(&list, "weak", -80, true);
    add_name(&list, "alpha", -40, true);
    assert(!network_manager_wifi_scan_list_add(
        &list, (const uint8_t *)"open", 4U, -20, false));
    add_name(&list, "weak", -30, true);
    add_name(&list, "weak", -60, true);
    add_name(&list, "beta", -40, true);
    network_manager_wifi_scan_list_finalize(&list);
    assert(3U == list.count);
    assert(0 == memcmp("weak", list.entries[0].ssid, 4U));
    assert(-30 == list.entries[0].rssi);
    assert(0 == memcmp("alpha", list.entries[1].ssid, 5U));
    assert(0 == memcmp("beta", list.entries[2].ssid, 4U));
    assert(!network_manager_wifi_scan_list_add(
        &list, (const uint8_t *)"bad\x1f", 4U, -10, true));
    assert(!network_manager_wifi_scan_list_add(
        &list, (const uint8_t *)"\xc0\xaf", 2U, -10, true));

    network_manager_wifi_scan_list_init(&list);
    for (size_t index = 0U;
         index < NETWORK_MANAGER_WIFI_SCAN_MAX_RESULTS - 1U; ++index) {
        uint8_t ssid[2] = {(uint8_t)('A' + index), 0U};
        assert(network_manager_wifi_scan_list_add(
            &list, ssid, 1U, (int8_t)(-90 + (int)index), true));
    }
    assert(NETWORK_MANAGER_WIFI_SCAN_MAX_RESULTS - 1U == list.count);
    add_name(&list, "Z", -10, true);
    assert(NETWORK_MANAGER_WIFI_SCAN_MAX_RESULTS == list.count);
    add_name(&list, "Y", -5, true);
    assert(NETWORK_MANAGER_WIFI_SCAN_MAX_RESULTS == list.count);
    network_manager_wifi_scan_list_finalize(&list);
    assert(1U == list.entries[0].ssid_len);
    assert('Y' == list.entries[0].ssid[0]);
    assert(-5 == list.entries[0].rssi);
    return 0;
}
