/*
 * Pure two-slot persistence model.
 *
 * The model owns encoding, slot selection, readback verification, and commit
 * order. Platform adapters only perform the requested storage operations.
 */
#include "network_manager_storage.h"

#include "network_manager_wifi_config.h"

#include <stddef.h>

static uint32_t crc32_update_byte(uint32_t crc, uint8_t value)
{
    crc ^= value;
    for (uint8_t bit = 0U; bit < 8U; ++bit) {
        crc = 0U != (crc & 1U) ?
            (crc >> 1U) ^ 0xEDB88320U : crc >> 1U;
    }
    return crc;
}

static uint32_t crc32_update_u16(uint32_t crc, uint16_t value)
{
    crc = crc32_update_byte(crc, (uint8_t)value);
    return crc32_update_byte(crc, (uint8_t)(value >> 8U));
}

static uint32_t crc32_update_u32(uint32_t crc, uint32_t value)
{
    for (uint8_t shift = 0U; shift < 32U; shift += 8U) {
        crc = crc32_update_byte(crc, (uint8_t)(value >> shift));
    }
    return crc;
}

static uint32_t blob_crc32(const network_manager_storage_blob_t *blob)
{
    /* Hash fields explicitly so compiler padding and host ABI do not matter. */
    uint32_t crc = UINT32_MAX;

    crc = crc32_update_u32(crc, blob->magic);
    crc = crc32_update_u16(crc, blob->schema_version);
    crc = crc32_update_u16(crc, blob->reserved);
    crc = crc32_update_u32(crc, blob->generation);
    crc = crc32_update_byte(crc, blob->ssid_len);
    crc = crc32_update_byte(crc, blob->password_len);
    for (size_t index = 0U; index < sizeof(blob->ssid); ++index) {
        crc = crc32_update_byte(crc, blob->ssid[index]);
    }
    for (size_t index = 0U; index < sizeof(blob->password); ++index) {
        crc = crc32_update_byte(crc, blob->password[index]);
    }
    return crc ^ UINT32_MAX;
}

static bool generation_is_newer(uint32_t candidate, uint32_t reference)
{
    /* Signed modular distance gives deterministic ordering across wrap. */
    return 0 < (int32_t)(candidate - reference);
}

static bool blobs_equal(const network_manager_storage_blob_t *left,
                        const network_manager_storage_blob_t *right)
{
    if (left->magic != right->magic ||
        left->schema_version != right->schema_version ||
        left->reserved != right->reserved ||
        left->generation != right->generation ||
        left->ssid_len != right->ssid_len ||
        left->password_len != right->password_len ||
        left->crc32 != right->crc32) {
        return false;
    }
    for (size_t index = 0U; index < sizeof(left->ssid); ++index) {
        if (left->ssid[index] != right->ssid[index]) {
            return false;
        }
    }
    for (size_t index = 0U; index < sizeof(left->password); ++index) {
        if (left->password[index] != right->password[index]) {
            return false;
        }
    }
    return true;
}

bool network_manager_storage_blob_encode(
    const network_manager_wifi_config_t *config,
    uint32_t generation,
    network_manager_storage_blob_t *blob)
{
    if (NULL == blob || 0U == generation) {
        return false;
    }

    network_manager_wifi_config_t sanitized;
    if (!network_manager_wifi_config_copy(&sanitized, config)) {
        return false;
    }

    unsigned char *bytes = (unsigned char *)blob;
    for (size_t index = 0U; index < sizeof(*blob); ++index) {
        bytes[index] = 0U;
    }
    blob->magic = NETWORK_MANAGER_STORAGE_MAGIC;
    blob->schema_version = NETWORK_MANAGER_STORAGE_SCHEMA_VERSION;
    blob->generation = generation;
    blob->ssid_len = sanitized.ssid_len;
    blob->password_len = sanitized.password_len;
    for (size_t index = 0U; index < sizeof(blob->ssid); ++index) {
        blob->ssid[index] = sanitized.ssid[index];
    }
    for (size_t index = 0U; index < sizeof(blob->password); ++index) {
        blob->password[index] = sanitized.password[index];
    }
    blob->crc32 = blob_crc32(blob);
    return true;
}

bool network_manager_storage_blob_is_valid(
    const network_manager_storage_blob_t *blob)
{
    if (NULL == blob || NETWORK_MANAGER_STORAGE_MAGIC != blob->magic ||
        NETWORK_MANAGER_STORAGE_SCHEMA_VERSION != blob->schema_version ||
        0U != blob->reserved || 0U == blob->generation) {
        return false;
    }

    network_manager_wifi_config_t config = {0};
    config.ssid_len = blob->ssid_len;
    config.password_len = blob->password_len;
    for (size_t index = 0U; index < sizeof(config.ssid); ++index) {
        config.ssid[index] = blob->ssid[index];
    }
    for (size_t index = 0U; index < sizeof(config.password); ++index) {
        config.password[index] = blob->password[index];
    }
    return network_manager_wifi_config_is_valid(&config) &&
           blob->crc32 == blob_crc32(blob);
}

network_manager_storage_load_result_t network_manager_storage_model_load(
    const network_manager_storage_image_t *image,
    network_manager_wifi_config_t *config,
    uint32_t *generation,
    network_manager_storage_slot_t *loaded_slot)
{
    if (NULL == image || NULL == config || NULL == generation ||
        NULL == loaded_slot) {
        return NETWORK_MANAGER_STORAGE_LOAD_INVALID_ARGUMENT;
    }
    const bool valid[2] = {
        image->slot_present[0] &&
            network_manager_storage_blob_is_valid(&image->slots[0]),
        image->slot_present[1] &&
            network_manager_storage_blob_is_valid(&image->slots[1]),
    };
    /* A valid selector is authoritative; otherwise use the newest valid slot. */
    size_t selected = 2U;
    if (image->selector_present &&
        (NETWORK_MANAGER_STORAGE_SLOT_A == image->selector ||
         NETWORK_MANAGER_STORAGE_SLOT_B == image->selector)) {
        const size_t requested = (size_t)image->selector;
        if (valid[requested]) {
            selected = requested;
        } else if (valid[1U - requested]) {
            selected = 1U - requested;
        }
    } else if (valid[0] && valid[1]) {
        selected = generation_is_newer(image->slots[1].generation,
                                       image->slots[0].generation) ? 1U : 0U;
    } else if (valid[0]) {
        selected = 0U;
    } else if (valid[1]) {
        selected = 1U;
    }
    if (2U == selected) {
        return image->slot_present[0] || image->slot_present[1] ?
            NETWORK_MANAGER_STORAGE_LOAD_CORRUPT :
            NETWORK_MANAGER_STORAGE_LOAD_NOT_FOUND;
    }

    const network_manager_storage_blob_t *blob = &image->slots[selected];
    network_manager_wifi_config_t loaded = {0};
    loaded.ssid_len = blob->ssid_len;
    loaded.password_len = blob->password_len;
    for (size_t index = 0U; index < sizeof(loaded.ssid); ++index) {
        loaded.ssid[index] = blob->ssid[index];
    }
    for (size_t index = 0U; index < sizeof(loaded.password); ++index) {
        loaded.password[index] = blob->password[index];
    }
    *config = loaded;
    *generation = blob->generation;
    *loaded_slot = (network_manager_storage_slot_t)selected;
    return NETWORK_MANAGER_STORAGE_LOAD_OK;
}

bool network_manager_storage_model_prepare_save(
    const network_manager_storage_image_t *image,
    const network_manager_wifi_config_t *config,
    network_manager_storage_save_plan_t *plan)
{
    if (NULL == image || NULL == config || NULL == plan) {
        return false;
    }

    network_manager_wifi_config_t current_config;
    uint32_t current_generation = 0U;
    network_manager_storage_slot_t current_slot =
        NETWORK_MANAGER_STORAGE_SLOT_NONE;
    const network_manager_storage_load_result_t load_result =
        network_manager_storage_model_load(image,
                                           &current_config,
                                           &current_generation,
                                           &current_slot);

    /* Always write the inactive slot so the selected old value stays intact. */
    network_manager_storage_slot_t target = NETWORK_MANAGER_STORAGE_SLOT_A;
    uint32_t next_generation = 1U;
    if (NETWORK_MANAGER_STORAGE_LOAD_OK == load_result) {
        target = NETWORK_MANAGER_STORAGE_SLOT_A == current_slot ?
            NETWORK_MANAGER_STORAGE_SLOT_B : NETWORK_MANAGER_STORAGE_SLOT_A;
        next_generation = UINT32_MAX == current_generation ?
            1U : current_generation + 1U;
    } else if (image->selector_present &&
               (NETWORK_MANAGER_STORAGE_SLOT_A == image->selector ||
                NETWORK_MANAGER_STORAGE_SLOT_B == image->selector)) {
        target = NETWORK_MANAGER_STORAGE_SLOT_A == image->selector ?
            NETWORK_MANAGER_STORAGE_SLOT_B : NETWORK_MANAGER_STORAGE_SLOT_A;
    }

    network_manager_storage_save_plan_t prepared = {
        .target_slot = target,
        .selector = target,
    };
    if (!network_manager_storage_blob_encode(config,
                                             next_generation,
                                             &prepared.blob)) {
        return false;
    }
    *plan = prepared;
    return true;
}

network_manager_storage_save_result_t network_manager_storage_execute_save(
    const network_manager_storage_image_t *image,
    const network_manager_wifi_config_t *config,
    const network_manager_storage_ops_t *ops)
{
    if (NULL == image || NULL == config || NULL == ops ||
        NULL == ops->write_blob || NULL == ops->commit ||
        NULL == ops->read_blob || NULL == ops->write_selector) {
        return NETWORK_MANAGER_STORAGE_SAVE_INVALID_ARGUMENT;
    }

    network_manager_storage_save_plan_t plan;
    if (!network_manager_storage_model_prepare_save(image, config, &plan)) {
        return NETWORK_MANAGER_STORAGE_SAVE_PREPARE_FAILED;
    }
    /* Commit and verify the new slot before publishing it via the selector. */
    if (!ops->write_blob(ops->context, plan.target_slot, &plan.blob)) {
        return NETWORK_MANAGER_STORAGE_SAVE_WRITE_FAILED;
    }
    if (!ops->commit(ops->context)) {
        return NETWORK_MANAGER_STORAGE_SAVE_SLOT_COMMIT_FAILED;
    }

    network_manager_storage_blob_t readback;
    if (!ops->read_blob(ops->context, plan.target_slot, &readback)) {
        return NETWORK_MANAGER_STORAGE_SAVE_READBACK_FAILED;
    }
    if (!network_manager_storage_blob_is_valid(&readback) ||
        !blobs_equal(&plan.blob, &readback)) {
        return NETWORK_MANAGER_STORAGE_SAVE_VERIFY_FAILED;
    }
    if (!ops->write_selector(ops->context, plan.selector)) {
        return NETWORK_MANAGER_STORAGE_SAVE_SELECTOR_WRITE_FAILED;
    }
    if (!ops->commit(ops->context)) {
        return NETWORK_MANAGER_STORAGE_SAVE_SELECTOR_COMMIT_FAILED;
    }
    return NETWORK_MANAGER_STORAGE_SAVE_OK;
}
