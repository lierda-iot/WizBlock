#include "network_manager_storage_nvs.h"

#include "network_manager_storage.h"

#include <stddef.h>

typedef struct {
    network_manager_storage_blob_t slots[2];
    bool slot_present[2];
    uint8_t selector;
    bool selector_present;
} fake_nvs_t;

static fake_nvs_t s_nvs;

static bool key_equal(const char *left, const char *right)
{
    size_t index = 0U;
    while ('\0' != left[index] && left[index] == right[index]) {
        ++index;
    }
    return left[index] == right[index];
}

static int slot_index(const char *key)
{
    if (key_equal(key, NETWORK_MANAGER_STORAGE_NVS_KEY_WIFI_A)) {
        return 0;
    }
    if (key_equal(key, NETWORK_MANAGER_STORAGE_NVS_KEY_WIFI_B)) {
        return 1;
    }
    return -1;
}

esp_err_t nvs_get_blob(nvs_handle_t handle,
                       const char *key,
                       void *value,
                       size_t *length)
{
    (void)handle;
    const int index = slot_index(key);
    if (0 > index || !s_nvs.slot_present[index]) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    if (NULL == value) {
        *length = sizeof(s_nvs.slots[index]);
        return ESP_OK;
    }
    if (*length < sizeof(s_nvs.slots[index])) {
        *length = sizeof(s_nvs.slots[index]);
        return ESP_ERR_NVS_INVALID_LENGTH;
    }
    *(network_manager_storage_blob_t *)value = s_nvs.slots[index];
    *length = sizeof(s_nvs.slots[index]);
    return ESP_OK;
}

esp_err_t nvs_set_blob(nvs_handle_t handle,
                       const char *key,
                       const void *value,
                       size_t length)
{
    (void)handle;
    const int index = slot_index(key);
    if (0 > index || sizeof(s_nvs.slots[index]) != length) {
        return ESP_ERR_INVALID_ARG;
    }
    s_nvs.slots[index] = *(const network_manager_storage_blob_t *)value;
    s_nvs.slot_present[index] = true;
    return ESP_OK;
}

esp_err_t nvs_get_u8(nvs_handle_t handle,
                     const char *key,
                     uint8_t *value)
{
    (void)handle;
    if (!key_equal(key, NETWORK_MANAGER_STORAGE_NVS_KEY_WIFI_SELECTOR) ||
        !s_nvs.selector_present) {
        return ESP_ERR_NVS_NOT_FOUND;
    }
    *value = s_nvs.selector;
    return ESP_OK;
}

esp_err_t nvs_set_u8(nvs_handle_t handle,
                     const char *key,
                     uint8_t value)
{
    (void)handle;
    if (!key_equal(key, NETWORK_MANAGER_STORAGE_NVS_KEY_WIFI_SELECTOR)) {
        return ESP_ERR_INVALID_ARG;
    }
    s_nvs.selector = value;
    s_nvs.selector_present = true;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key)
{
    (void)handle;
    const int index = slot_index(key);
    if (0 <= index) {
        if (!s_nvs.slot_present[index]) {
            return ESP_ERR_NVS_NOT_FOUND;
        }
        s_nvs.slot_present[index] = false;
        return ESP_OK;
    }
    if (key_equal(key, NETWORK_MANAGER_STORAGE_NVS_KEY_WIFI_SELECTOR)) {
        if (!s_nvs.selector_present) {
            return ESP_ERR_NVS_NOT_FOUND;
        }
        s_nvs.selector_present = false;
        return ESP_OK;
    }
    return ESP_ERR_NVS_NOT_FOUND;
}

esp_err_t nvs_commit(nvs_handle_t handle)
{
    (void)handle;
    return ESP_OK;
}

static network_manager_wifi_config_t make_config(uint8_t first_ssid_byte)
{
    network_manager_wifi_config_t config = {0};
    config.ssid[0] = first_ssid_byte;
    config.ssid_len = 1U;
    for (uint8_t index = 0U; index < 8U; ++index) {
        config.password[index] = 'x';
    }
    config.password_len = 8U;
    return config;
}

int main(void)
{
    const nvs_handle_t handle = 1U;
    const network_manager_wifi_config_t first = make_config('a');
    const network_manager_wifi_config_t second = make_config('b');
    network_manager_wifi_config_t loaded;
    bool present = false;
    uint32_t generation = 0U;

    if (ESP_OK != network_manager_storage_nvs_save(handle, &first)) {
        return 1;
    }
    if (ESP_OK != network_manager_storage_nvs_load(
            handle, &loaded, &present, &generation) ||
        !present || 1U != generation || 'a' != loaded.ssid[0]) {
        return 2;
    }
    if (ESP_OK != network_manager_storage_nvs_save(handle, &second)) {
        return 3;
    }
    if (ESP_OK != network_manager_storage_nvs_load(
            handle, &loaded, &present, &generation) ||
        !present || 2U != generation || 'b' != loaded.ssid[0]) {
        return 4;
    }
    if (ESP_OK != network_manager_storage_nvs_clear(handle)) {
        return 5;
    }
    if (ESP_OK != network_manager_storage_nvs_load(
            handle, &loaded, &present, &generation) ||
        present || 0U != generation) {
        return 6;
    }
    return 0;
}
