#pragma once

#include "notifier_model.h"
#include "notifier_wifi_config.h"

#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef bool (*notifier_ui_copy_model_fn)(notifier_model_t *destination,
                                           void *user_context);
typedef esp_err_t (*notifier_ui_save_wifi_fn)(
    const notifier_wifi_credentials_t *credentials, void *user_context);
typedef esp_err_t (*notifier_ui_request_wifi_scan_fn)(void *user_context);

esp_err_t notifier_ui_start(notifier_ui_copy_model_fn copy_model,
                            void *model_context,
                            const notifier_wifi_credentials_t *credentials,
                            bool has_credentials,
                            notifier_ui_save_wifi_fn save_wifi,
                            notifier_ui_request_wifi_scan_fn request_wifi_scan,
                            void *wifi_context);
esp_err_t notifier_ui_publish_wifi_scan(
    const notifier_wifi_scan_list_t *networks, esp_err_t scan_result);
uint32_t notifier_ui_flush_error_count(void);
