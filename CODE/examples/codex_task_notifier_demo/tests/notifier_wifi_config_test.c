#include "notifier_wifi_config.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>

static void fill_text(char *text, size_t length, char value)
{
    assert(NULL != text);
    memset(text, value, length);
    text[length] = '\0';
}

static void test_ssid_length_boundaries(void)
{
    char ssid[NOTIFIER_WIFI_SSID_MAX_BYTES + 2U] = {0};

    assert(NOTIFIER_WIFI_CONFIG_SSID_REQUIRED ==
           notifier_wifi_config_validate(NULL, "password"));
    assert(NOTIFIER_WIFI_CONFIG_SSID_REQUIRED ==
           notifier_wifi_config_validate("", "password"));
    assert(NOTIFIER_WIFI_CONFIG_OK ==
           notifier_wifi_config_validate("a", "password"));

    fill_text(ssid, NOTIFIER_WIFI_SSID_MAX_BYTES, 's');
    assert(NOTIFIER_WIFI_CONFIG_OK ==
           notifier_wifi_config_validate(ssid, "password"));
    fill_text(ssid, NOTIFIER_WIFI_SSID_MAX_BYTES + 1U, 's');
    assert(NOTIFIER_WIFI_CONFIG_SSID_TOO_LONG ==
           notifier_wifi_config_validate(ssid, "password"));
}

static void test_password_boundaries_and_characters(void)
{
    char password[NOTIFIER_WIFI_PASSWORD_MAX_BYTES + 2U] = {0};

    assert(NOTIFIER_WIFI_CONFIG_OK ==
           notifier_wifi_config_validate("ssid", NULL));
    assert(NOTIFIER_WIFI_CONFIG_OK ==
           notifier_wifi_config_validate("ssid", ""));
    assert(NOTIFIER_WIFI_CONFIG_PASSWORD_LENGTH ==
           notifier_wifi_config_validate("ssid", "1234567"));
    assert(NOTIFIER_WIFI_CONFIG_OK ==
           notifier_wifi_config_validate("ssid", "12345678"));

    fill_text(password, NOTIFIER_WIFI_PASSWORD_MAX_BYTES, 'p');
    assert(NOTIFIER_WIFI_CONFIG_OK ==
           notifier_wifi_config_validate("ssid", password));
    fill_text(password, NOTIFIER_WIFI_PASSWORD_MAX_BYTES + 1U, 'p');
    assert(NOTIFIER_WIFI_CONFIG_PASSWORD_LENGTH ==
           notifier_wifi_config_validate("ssid", password));

    strcpy(password, "12345678");
    password[3] = '\n';
    assert(NOTIFIER_WIFI_CONFIG_PASSWORD_CHARACTER ==
           notifier_wifi_config_validate("ssid", password));
    password[3] = (char)0x7F;
    assert(NOTIFIER_WIFI_CONFIG_PASSWORD_CHARACTER ==
           notifier_wifi_config_validate("ssid", password));
}

static void test_copy_is_complete_or_zeroed(void)
{
    notifier_wifi_credentials_t credentials = {0};

    assert(NOTIFIER_WIFI_CONFIG_OK == notifier_wifi_credentials_set(
               &credentials, "office wifi", "12345678"));
    assert(0 == strcmp(credentials.ssid, "office wifi"));
    assert(0 == strcmp(credentials.password, "12345678"));

    assert(NOTIFIER_WIFI_CONFIG_PASSWORD_LENGTH ==
           notifier_wifi_credentials_set(&credentials, "new", "short"));
    assert('\0' == credentials.ssid[0]);
    assert('\0' == credentials.password[0]);
    assert(NOTIFIER_WIFI_CONFIG_INVALID_ARGUMENT ==
           notifier_wifi_credentials_set(NULL, "ssid", "12345678"));
}

static void test_scan_filters_deduplicates_and_sorts(void)
{
    notifier_wifi_scan_list_t list = {0};
    char long_ssid[NOTIFIER_WIFI_SSID_MAX_BYTES + 2U] = {0};
    const char invalid_utf8[] = {(char)0xC0, (char)0xAF, '\0'};

    notifier_wifi_scan_list_init(&list);
    assert(!notifier_wifi_scan_list_add(NULL, "Office", -40));
    assert(!notifier_wifi_scan_list_add(&list, NULL, -40));
    assert(!notifier_wifi_scan_list_add(&list, "", -40));
    assert(!notifier_wifi_scan_list_add(&list, "line\nbreak", -40));
    assert(!notifier_wifi_scan_list_add(&list, invalid_utf8, -40));
    fill_text(long_ssid, NOTIFIER_WIFI_SSID_MAX_BYTES + 1U, 'x');
    assert(!notifier_wifi_scan_list_add(&list, long_ssid, -40));

    assert(notifier_wifi_scan_list_add(&list, "Office", -75));
    assert(notifier_wifi_scan_list_add(&list, "Guest", -40));
    assert(notifier_wifi_scan_list_add(&list, "Alpha", -40));
    assert(notifier_wifi_scan_list_add(&list, "Office", -35));
    assert(notifier_wifi_scan_list_add(&list, "\xE5\xAE\xA2\xE5\x8E\x85", -60));
    notifier_wifi_scan_list_finalize(&list, NULL);

    assert(4U == list.count);
    assert(0 == strcmp(list.networks[0].ssid, "Office"));
    assert(-35 == list.networks[0].rssi);
    assert(0 == strcmp(list.networks[1].ssid, "Alpha"));
    assert(0 == strcmp(list.networks[2].ssid, "Guest"));
    assert(0 == strcmp(list.networks[3].ssid,
                       "\xE5\xAE\xA2\xE5\x8E\x85"));
}

static void test_scan_capacity_keeps_strongest_networks(void)
{
    notifier_wifi_scan_list_t list = {0};
    char ssid[NOTIFIER_WIFI_SSID_MAX_BYTES + 1U] = {0};
    size_t index = 0U;

    notifier_wifi_scan_list_init(&list);
    for (index = 0U; index < NOTIFIER_WIFI_SCAN_MAX_NETWORKS + 1U;
         index++) {
        (void)snprintf(ssid, sizeof(ssid), "network-%02u",
                       (unsigned)index);
        assert(notifier_wifi_scan_list_add(&list, ssid,
                                           (int8_t)(-100 + (int)index)));
    }
    notifier_wifi_scan_list_finalize(&list, NULL);

    assert(NOTIFIER_WIFI_SCAN_MAX_NETWORKS == list.count);
    assert(0 == strcmp(list.networks[0].ssid, "network-20"));
    assert(0 == strcmp(list.networks[list.count - 1U].ssid, "network-01"));
}

static void test_scan_preserves_and_selects_saved_network(void)
{
    notifier_wifi_scan_list_t list = {0};
    char ssid[NOTIFIER_WIFI_SSID_MAX_BYTES + 1U] = {0};
    size_t index = 0U;

    notifier_wifi_scan_list_init(&list);
    assert(notifier_wifi_scan_list_add(&list, "Office", -30));
    assert(notifier_wifi_scan_list_add(&list, "Saved", -60));
    notifier_wifi_scan_list_finalize(&list, "Saved");
    assert(2U == list.count);
    assert(1U == list.selected_index);
    assert(0 == strcmp(list.networks[list.selected_index].ssid, "Saved"));

    notifier_wifi_scan_list_init(&list);
    for (index = 0U; index < NOTIFIER_WIFI_SCAN_MAX_NETWORKS; index++) {
        (void)snprintf(ssid, sizeof(ssid), "network-%02u",
                       (unsigned)index);
        assert(notifier_wifi_scan_list_add(&list, ssid,
                                           (int8_t)(-30 - (int)index)));
    }
    notifier_wifi_scan_list_finalize(&list, "Saved");
    assert(NOTIFIER_WIFI_SCAN_MAX_NETWORKS == list.count);
    assert(0U == list.selected_index);
    assert(0 == strcmp(list.networks[0].ssid, "Saved"));
    assert(0 == strcmp(list.networks[1].ssid, "network-00"));
    assert(0 == strcmp(list.networks[list.count - 1U].ssid, "network-18"));
}

int main(void)
{
    test_ssid_length_boundaries();
    test_password_boundaries_and_characters();
    test_copy_is_complete_or_zeroed();
    test_scan_filters_deduplicates_and_sorts();
    test_scan_capacity_keeps_strongest_networks();
    test_scan_preserves_and_selects_saved_network();
    puts("notifier_wifi_config_test: PASS");
    return 0;
}
