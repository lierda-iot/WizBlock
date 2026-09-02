/* ESP-IDF NVS adapter for the platform-independent two-slot storage model. */
#include "network_manager_storage_nvs.h"

#include "network_manager_storage.h"

#include <stddef.h>

typedef struct {
    nvs_handle_t handle;
    esp_err_t last_error;
} storage_nvs_context_t;

static const char *slot_key(network_manager_storage_slot_t slot)
{
    return NETWORK_MANAGER_STORAGE_SLOT_A == slot ?
        NETWORK_MANAGER_STORAGE_NVS_KEY_WIFI_A :
        NETWORK_MANAGER_STORAGE_NVS_KEY_WIFI_B;
}

static esp_err_t read_slot(nvs_handle_t handle,
                           network_manager_storage_slot_t slot,
                           network_manager_storage_blob_t *blob,
                           bool *present)
{
    size_t size = 0U;
    esp_err_t result = nvs_get_blob(handle, slot_key(slot), NULL, &size);
    if (ESP_ERR_NVS_NOT_FOUND == result) {
        *present = false;
        return ESP_OK;
    }
    if (ESP_OK != result) {
        return result;
    }

    /* A wrong-sized value is present but invalid; the model classifies it. */
    *present = true;
    if (sizeof(*blob) != size) {
        *blob = (network_manager_storage_blob_t){0};
        return ESP_OK;
    }
    return nvs_get_blob(handle, slot_key(slot), blob, &size);
}

static esp_err_t load_image(nvs_handle_t handle,
                            network_manager_storage_image_t *image)
{
    *image = (network_manager_storage_image_t){0};
    esp_err_t result = read_slot(handle,
                                 NETWORK_MANAGER_STORAGE_SLOT_A,
                                 &image->slots[NETWORK_MANAGER_STORAGE_SLOT_A],
                                 &image->slot_present[
                                     NETWORK_MANAGER_STORAGE_SLOT_A]);
    if (ESP_OK != result) {
        return result;
    }
    result = read_slot(handle,
                       NETWORK_MANAGER_STORAGE_SLOT_B,
                       &image->slots[NETWORK_MANAGER_STORAGE_SLOT_B],
                       &image->slot_present[NETWORK_MANAGER_STORAGE_SLOT_B]);
    if (ESP_OK != result) {
        return result;
    }

    uint8_t selector = 0U;
    result = nvs_get_u8(handle,
                        NETWORK_MANAGER_STORAGE_NVS_KEY_WIFI_SELECTOR,
                        &selector);
    if (ESP_ERR_NVS_NOT_FOUND == result) {
        return ESP_OK;
    }
    if (ESP_OK != result) {
        return result;
    }
    image->selector_present = true;
    image->selector = (network_manager_storage_slot_t)selector;
    return ESP_OK;
}

static bool nvs_write_blob(void *context,
                           network_manager_storage_slot_t slot,
                           const network_manager_storage_blob_t *blob)
{
    storage_nvs_context_t *nvs_context = (storage_nvs_context_t *)context;
    nvs_context->last_error = nvs_set_blob(nvs_context->handle,
                                           slot_key(slot),
                                           blob,
                                           sizeof(*blob));
    return ESP_OK == nvs_context->last_error;
}

static bool nvs_commit_changes(void *context)
{
    storage_nvs_context_t *nvs_context = (storage_nvs_context_t *)context;
    nvs_context->last_error = nvs_commit(nvs_context->handle);
    return ESP_OK == nvs_context->last_error;
}

static bool nvs_read_blob(void *context,
                          network_manager_storage_slot_t slot,
                          network_manager_storage_blob_t *blob)
{
    storage_nvs_context_t *nvs_context = (storage_nvs_context_t *)context;
    size_t size = sizeof(*blob);
    nvs_context->last_error = nvs_get_blob(nvs_context->handle,
                                           slot_key(slot),
                                           blob,
                                           &size);
    if (ESP_OK == nvs_context->last_error && sizeof(*blob) != size) {
        nvs_context->last_error = ESP_ERR_NVS_INVALID_LENGTH;
    }
    return ESP_OK == nvs_context->last_error;
}

static bool nvs_write_selector(void *context,
                               network_manager_storage_slot_t selector)
{
    storage_nvs_context_t *nvs_context = (storage_nvs_context_t *)context;
    nvs_context->last_error = nvs_set_u8(
        nvs_context->handle,
        NETWORK_MANAGER_STORAGE_NVS_KEY_WIFI_SELECTOR,
        (uint8_t)selector);
    return ESP_OK == nvs_context->last_error;
}

esp_err_t network_manager_storage_nvs_load(
    nvs_handle_t handle,
    network_manager_wifi_config_t *config,
    bool *present,
    uint32_t *generation)
{
    if (NULL == config || NULL == present || NULL == generation) {
        return ESP_ERR_INVALID_ARG;
    }

    network_manager_storage_image_t image;
    esp_err_t result = load_image(handle, &image);
    if (ESP_OK != result) {
        return result;
    }

    network_manager_storage_slot_t loaded_slot =
        NETWORK_MANAGER_STORAGE_SLOT_NONE;
    const network_manager_storage_load_result_t load_result =
        network_manager_storage_model_load(&image,
                                           config,
                                           generation,
                                           &loaded_slot);
    if (NETWORK_MANAGER_STORAGE_LOAD_NOT_FOUND == load_result) {
        *present = false;
        *generation = 0U;
        return ESP_OK;
    }
    if (NETWORK_MANAGER_STORAGE_LOAD_OK != load_result) {
        *present = false;
        *generation = 0U;
        return ESP_ERR_INVALID_CRC;
    }
    *present = true;
    return ESP_OK;
}

esp_err_t network_manager_storage_nvs_save(
    nvs_handle_t handle,
    const network_manager_wifi_config_t *config)
{
    if (NULL == config) {
        return ESP_ERR_INVALID_ARG;
    }

    network_manager_storage_image_t image;
    esp_err_t result = load_image(handle, &image);
    if (ESP_OK != result) {
        return result;
    }

    /* Preserve the exact NVS error while the model works with boolean ops. */
    storage_nvs_context_t context = {
        .handle = handle,
        .last_error = ESP_OK,
    };
    const network_manager_storage_ops_t ops = {
        .context = &context,
        .write_blob = nvs_write_blob,
        .commit = nvs_commit_changes,
        .read_blob = nvs_read_blob,
        .write_selector = nvs_write_selector,
    };
    const network_manager_storage_save_result_t save_result =
        network_manager_storage_execute_save(&image, config, &ops);
    if (NETWORK_MANAGER_STORAGE_SAVE_OK == save_result) {
        return ESP_OK;
    }
    if (NETWORK_MANAGER_STORAGE_SAVE_PREPARE_FAILED == save_result ||
        NETWORK_MANAGER_STORAGE_SAVE_INVALID_ARGUMENT == save_result) {
        return ESP_ERR_INVALID_ARG;
    }
    if (NETWORK_MANAGER_STORAGE_SAVE_VERIFY_FAILED == save_result) {
        return ESP_ERR_INVALID_CRC;
    }
    return ESP_OK != context.last_error ? context.last_error : ESP_FAIL;
}

esp_err_t network_manager_storage_nvs_clear(nvs_handle_t handle)
{
    /* Erase only keys owned by this component; never erase the NVS partition. */
    const char *keys[] = {
        NETWORK_MANAGER_STORAGE_NVS_KEY_WIFI_A,
        NETWORK_MANAGER_STORAGE_NVS_KEY_WIFI_B,
        NETWORK_MANAGER_STORAGE_NVS_KEY_WIFI_SELECTOR,
    };
    for (size_t index = 0U; index < sizeof(keys) / sizeof(keys[0]); ++index) {
        const esp_err_t result = nvs_erase_key(handle, keys[index]);
        if (ESP_OK != result && ESP_ERR_NVS_NOT_FOUND != result) {
            return result;
        }
    }
    return nvs_commit(handle);
}
