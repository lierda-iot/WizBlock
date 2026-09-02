#include "network_manager_wifi_runtime_model.h"

int main(void)
{
    network_manager_wifi_runtime_model_t model;
    network_manager_wifi_runtime_output_t output;
    uint32_t now_ms = 0U;

    network_manager_wifi_runtime_model_init(&model);
    for (uint16_t failure = 0U; failure < 300U; ++failure) {
        network_manager_wifi_runtime_model_on_connect_failed(
            &model, now_ms, &output);
        if (model.retry_exhausted || output.retry_exhausted_changed ||
            !model.retry_deadline_active) {
            return 1;
        }
        now_ms += model.retry_delay_ms;
        network_manager_wifi_runtime_model_tick(&model, now_ms, &output);
        if (!output.connect_retry || model.retry_deadline_active) {
            return 2;
        }
    }
    if (UINT8_MAX != model.retry_attempt) {
        return 3;
    }

    network_manager_wifi_runtime_model_on_connect_failed(
        &model, now_ms, &output);
    network_manager_wifi_runtime_model_set_automatic_recovery(&model, false);
    if (model.automatic_recovery_enabled || model.retry_deadline_active) {
        return 4;
    }
    network_manager_wifi_runtime_model_on_connect_failed(
        &model, now_ms, &output);
    if (output.connect_retry || model.retry_deadline_active ||
        UINT8_MAX != model.retry_attempt) {
        return 5;
    }
    return 0;
}
