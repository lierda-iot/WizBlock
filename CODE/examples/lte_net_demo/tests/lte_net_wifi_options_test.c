#include "lte_net_wifi_options.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void add_scan_entry(network_manager_wifi_scan_list_t *list,
                           const char *ssid,
                           int8_t rssi)
{
    const size_t length = strlen(ssid);
    assert(NULL != list);
    assert(length <= NETWORK_MANAGER_WIFI_SSID_MAX_BYTES);
    assert(list->count < NETWORK_MANAGER_WIFI_SCAN_MAX_RESULTS);

    network_manager_wifi_scan_entry_t *entry = &list->entries[list->count++];
    memcpy(entry->ssid, ssid, length);
    entry->ssid_len = (uint8_t)length;
    entry->rssi = rssi;
    entry->secure = true;
}

static void test_saved_ssid_is_appended_and_selected(void)
{
    network_manager_wifi_scan_list_t scan = {0};
    lte_net_wifi_options_t model = {0};
    char options[256] = {0};
    char selected[NETWORK_MANAGER_WIFI_SSID_MAX_BYTES + 1U] = {0};

    add_scan_entry(&scan, "Office", -40);
    add_scan_entry(&scan, "Guest", -61);

    assert(lte_net_wifi_options_build(&scan, "Saved", options,
                                      sizeof(options), &model));
    assert(0 == strcmp("Office\nGuest\nSaved\nOther network...", options));
    assert(2U == model.visible_scan_count);
    assert(model.preferred_appended);
    assert(2U == model.preferred_index);
    assert(3U == model.other_index);
    assert(2U == model.selected_index);
    assert(lte_net_wifi_options_copy_selection(&scan, &model,
                                               model.selected_index,
                                               selected, sizeof(selected)));
    assert(0 == strcmp("Saved", selected));
}

static void test_saved_ssid_is_not_duplicated(void)
{
    network_manager_wifi_scan_list_t scan = {0};
    lte_net_wifi_options_t model = {0};
    char options[192] = {0};

    add_scan_entry(&scan, "Home", -35);
    add_scan_entry(&scan, "Office", -48);

    assert(lte_net_wifi_options_build(&scan, "Office", options,
                                      sizeof(options), &model));
    assert(0 == strcmp("Home\nOffice\nOther network...", options));
    assert(!model.preferred_appended);
    assert(1U == model.selected_index);
    assert(2U == model.other_index);
}

static void test_empty_scan_keeps_manual_entry_available(void)
{
    network_manager_wifi_scan_list_t scan = {0};
    lte_net_wifi_options_t model = {0};
    char options[64] = {0};
    char selected[NETWORK_MANAGER_WIFI_SSID_MAX_BYTES + 1U] = {0};

    assert(lte_net_wifi_options_build(&scan, NULL, options, sizeof(options),
                                      &model));
    assert(0 == strcmp("Other network...", options));
    assert(0U == model.visible_scan_count);
    assert(0U == model.other_index);
    assert(0U == model.selected_index);
    assert(!lte_net_wifi_options_copy_selection(&scan, &model,
                                                model.other_index, selected,
                                                sizeof(selected)));
}

static void test_invalid_scan_entry_is_skipped_without_breaking_mapping(void)
{
    network_manager_wifi_scan_list_t scan = {0};
    lte_net_wifi_options_t model = {0};
    char options[128] = {0};
    char selected[NETWORK_MANAGER_WIFI_SSID_MAX_BYTES + 1U] = {0};

    add_scan_entry(&scan, "Good", -45);
    scan.entries[scan.count].ssid[0] = '\n';
    scan.entries[scan.count].ssid_len = 1U;
    scan.entries[scan.count].secure = true;
    scan.count++;
    add_scan_entry(&scan, "Second", -60);

    assert(lte_net_wifi_options_build(&scan, NULL, options, sizeof(options),
                                      &model));
    assert(0 == strcmp("Good\nSecond\nOther network...", options));
    assert(2U == model.visible_scan_count);
    assert(2U == model.source_indices[1]);
    assert(lte_net_wifi_options_copy_selection(&scan, &model, 1U, selected,
                                               sizeof(selected)));
    assert(0 == strcmp("Second", selected));
}

static void test_small_output_buffer_fails_without_partial_options(void)
{
    network_manager_wifi_scan_list_t scan = {0};
    lte_net_wifi_options_t model = {0};
    char options[12] = "unchanged";

    add_scan_entry(&scan, "Office", -40);
    assert(!lte_net_wifi_options_build(&scan, "Saved", options,
                                       sizeof(options), &model));
    assert('\0' == options[0]);
}

static void test_scan_result_must_match_pending_operation(void)
{
    network_manager_wifi_scan_list_t scan = {.operation_id = 41U};

    assert(!lte_net_wifi_scan_result_matches(41U, false, &scan));
    assert(!lte_net_wifi_scan_result_matches(42U, true, &scan));
    assert(lte_net_wifi_scan_result_matches(41U, true, &scan));
    assert(!lte_net_wifi_scan_result_matches(41U, true, NULL));
}

int main(void)
{
    test_saved_ssid_is_appended_and_selected();
    test_saved_ssid_is_not_duplicated();
    test_empty_scan_keeps_manual_entry_available();
    test_invalid_scan_entry_is_skipped_without_breaking_mapping();
    test_small_output_buffer_fails_without_partial_options();
    test_scan_result_must_match_pending_operation();
    puts("lte_net_wifi_options_test: PASS");
    return 0;
}
