#include "lte_net_wifi_options.h"

#include <string.h>

static bool ssid_is_dropdown_safe(const uint8_t *ssid, size_t length)
{
    if (NULL == ssid || 0U == length ||
        NETWORK_MANAGER_WIFI_SSID_MAX_BYTES < length) {
        return false;
    }
    for (size_t index = 0U; index < length; ++index) {
        if (ssid[index] < 0x20U || 0x7FU == ssid[index]) {
            return false;
        }
    }
    return true;
}

static bool append_option(char *options, size_t capacity, size_t *offset,
                          const uint8_t *text, size_t length)
{
    const size_t separator_length = (0U == *offset) ? 0U : 1U;

    if (NULL == options || NULL == offset || NULL == text ||
        *offset >= capacity ||
        capacity - *offset <= separator_length + length) {
        return false;
    }
    if (0U != separator_length) {
        options[(*offset)++] = '\n';
    }
    memcpy(&options[*offset], text, length);
    *offset += length;
    options[*offset] = '\0';
    return true;
}

static bool ssid_matches_text(const uint8_t *ssid, size_t ssid_length,
                              const char *text)
{
    if (NULL == text) {
        return false;
    }
    const size_t text_length = strlen(text);
    return ssid_length == text_length &&
           0 == memcmp(ssid, text, ssid_length);
}

bool lte_net_wifi_options_build(
    const network_manager_wifi_scan_list_t *scan,
    const char *preferred_ssid,
    char *options,
    size_t options_capacity,
    lte_net_wifi_options_t *model)
{
    size_t offset = 0U;
    bool preferred_found = false;
    size_t scan_count = 0U;

    if (NULL == scan || NULL == options || 0U == options_capacity ||
        NULL == model) {
        return false;
    }
    options[0] = '\0';
    *model = (lte_net_wifi_options_t){0};
    scan_count = (scan->count < NETWORK_MANAGER_WIFI_SCAN_MAX_RESULTS) ?
                 scan->count : NETWORK_MANAGER_WIFI_SCAN_MAX_RESULTS;

    for (size_t source_index = 0U; source_index < scan_count; ++source_index) {
        const network_manager_wifi_scan_entry_t *entry =
            &scan->entries[source_index];
        if (!ssid_is_dropdown_safe(entry->ssid, entry->ssid_len)) {
            continue;
        }
        if (!append_option(options, options_capacity, &offset, entry->ssid,
                           entry->ssid_len)) {
            options[0] = '\0';
            *model = (lte_net_wifi_options_t){0};
            return false;
        }
        const size_t visible_index = model->visible_scan_count;
        model->source_indices[visible_index] = source_index;
        model->visible_scan_count++;
        if (!preferred_found &&
            ssid_matches_text(entry->ssid, entry->ssid_len, preferred_ssid)) {
            preferred_found = true;
            model->selected_index = visible_index;
        }
    }

    const size_t preferred_length = (NULL != preferred_ssid) ?
                                    strlen(preferred_ssid) : 0U;
    if (!preferred_found &&
        ssid_is_dropdown_safe((const uint8_t *)preferred_ssid,
                              preferred_length)) {
        if (!append_option(options, options_capacity, &offset,
                           (const uint8_t *)preferred_ssid,
                           preferred_length)) {
            options[0] = '\0';
            *model = (lte_net_wifi_options_t){0};
            return false;
        }
        model->preferred_appended = true;
        memcpy(model->preferred_ssid, preferred_ssid, preferred_length);
        model->preferred_ssid_len = (uint8_t)preferred_length;
        model->preferred_index = model->visible_scan_count;
        model->selected_index = model->preferred_index;
    }

    model->other_index = model->visible_scan_count +
                         (model->preferred_appended ? 1U : 0U);
    if (!append_option(options, options_capacity, &offset,
                       (const uint8_t *)LTE_NET_WIFI_OTHER_OPTION,
                       sizeof(LTE_NET_WIFI_OTHER_OPTION) - 1U)) {
        options[0] = '\0';
        *model = (lte_net_wifi_options_t){0};
        return false;
    }
    if (0U == model->visible_scan_count && !model->preferred_appended) {
        model->selected_index = model->other_index;
    }
    return true;
}

bool lte_net_wifi_options_copy_selection(
    const network_manager_wifi_scan_list_t *scan,
    const lte_net_wifi_options_t *model,
    size_t selected_index,
    char *ssid,
    size_t ssid_capacity)
{
    const uint8_t *source = NULL;
    size_t length = 0U;

    if (NULL == scan || NULL == model || NULL == ssid ||
        0U == ssid_capacity || selected_index == model->other_index) {
        return false;
    }
    if (selected_index < model->visible_scan_count) {
        const size_t source_index = model->source_indices[selected_index];
        if (source_index >= scan->count) {
            return false;
        }
        source = scan->entries[source_index].ssid;
        length = scan->entries[source_index].ssid_len;
    } else if (model->preferred_appended &&
               selected_index == model->preferred_index) {
        source = model->preferred_ssid;
        length = model->preferred_ssid_len;
    } else {
        return false;
    }
    if (!ssid_is_dropdown_safe(source, length) || ssid_capacity <= length) {
        return false;
    }
    memcpy(ssid, source, length);
    ssid[length] = '\0';
    return true;
}

bool lte_net_wifi_scan_result_matches(uint32_t pending_operation_id,
                                      bool scan_pending,
                                      const network_manager_wifi_scan_list_t *scan)
{
    return scan_pending && 0U != pending_operation_id && NULL != scan &&
           pending_operation_id == scan->operation_id;
}
