#include "network_manager_wifi_runtime_model.h"

static uint32_t expected_retry_delay(uint8_t attempt)
{
    uint32_t delay_ms = NETWORK_MANAGER_WIFI_RETRY_INITIAL_BACKOFF_MS;
    for (uint8_t index = 1U; index < attempt; ++index) {
        if (delay_ms >= NETWORK_MANAGER_WIFI_RETRY_MAX_BACKOFF_MS ||
            delay_ms > NETWORK_MANAGER_WIFI_RETRY_MAX_BACKOFF_MS / 2U) {
            return NETWORK_MANAGER_WIFI_RETRY_MAX_BACKOFF_MS;
        }
        delay_ms *= 2U;
    }
    return delay_ms > NETWORK_MANAGER_WIFI_RETRY_MAX_BACKOFF_MS ?
        NETWORK_MANAGER_WIFI_RETRY_MAX_BACKOFF_MS : delay_ms;
}

int main(void)
{
    network_manager_wifi_runtime_model_t model;
    network_manager_wifi_runtime_output_t output;

    network_manager_wifi_runtime_model_init(&model);
    network_manager_wifi_runtime_model_update_raw(
        &model, true, true, true, 100U, &output);
    if (!model.raw_link_up || !model.raw_ipv4_ready || model.stable_ready ||
        output.report_connected) {
        return 1;
    }
    network_manager_wifi_runtime_model_tick(
        &model, 100U + NETWORK_MANAGER_WIFI_CONNECT_STABLE_MS - 1U, &output);
    if (model.stable_ready || output.report_connected) {
        return 2;
    }
    network_manager_wifi_runtime_model_tick(
        &model, 100U + NETWORK_MANAGER_WIFI_CONNECT_STABLE_MS, &output);
    if (!model.stable_link_up || !model.stable_ipv4_ready ||
        !model.stable_ready || !output.report_connected) {
        return 3;
    }
    network_manager_wifi_runtime_model_update_raw(
        &model, false, false, true, 20000U, &output);
    if (!output.report_disconnected || !model.stable_ready ||
        !model.disconnect_stability_active) {
        return 4;
    }
    network_manager_wifi_runtime_model_tick(
        &model, 20000U + NETWORK_MANAGER_WIFI_DISCONNECT_STABLE_MS - 1U,
        &output);
    if (!model.stable_ready || output.stable_changed) {
        return 5;
    }
    network_manager_wifi_runtime_model_tick(
        &model, 20000U + NETWORK_MANAGER_WIFI_DISCONNECT_STABLE_MS, &output);
    if (model.stable_link_up || model.stable_ipv4_ready ||
        model.stable_ready || !output.stable_changed) {
        return 6;
    }

    network_manager_wifi_runtime_model_init(&model);
    network_manager_wifi_runtime_model_update_raw(
        &model, true, true, true, 0U, &output);
    network_manager_wifi_runtime_model_tick(
        &model, NETWORK_MANAGER_WIFI_CONNECT_STABLE_MS, &output);
    model.retry_attempt = 5U;
    network_manager_wifi_runtime_model_update_raw(
        &model, false, false, true, 20000U, &output);
    network_manager_wifi_runtime_model_update_raw(
        &model, true, true, true, 22000U, &output);
    if (!model.stable_ready || model.disconnect_stability_active ||
        output.report_connected || 5U != model.retry_attempt) {
        return 7;
    }
    network_manager_wifi_runtime_model_tick(
        &model, 22000U + NETWORK_MANAGER_WIFI_CONNECT_STABLE_MS - 1U,
        &output);
    if (5U != model.retry_attempt) {
        return 8;
    }
    network_manager_wifi_runtime_model_tick(
        &model, 22000U + NETWORK_MANAGER_WIFI_CONNECT_STABLE_MS, &output);
    if (0U != model.retry_attempt || !output.retry_budget_reset) {
        return 9;
    }

    const uint32_t wrap_start = UINT32_MAX -
                                (NETWORK_MANAGER_WIFI_CONNECT_STABLE_MS - 2U);
    network_manager_wifi_runtime_model_init(&model);
    network_manager_wifi_runtime_model_update_raw(
        &model, true, true, true, wrap_start, &output);
    network_manager_wifi_runtime_model_tick(&model, 0U, &output);
    if (model.stable_ready) {
        return 10;
    }
    network_manager_wifi_runtime_model_tick(&model, 1U, &output);
    if (!model.stable_ready || !output.report_connected) {
        return 11;
    }
    network_manager_wifi_runtime_model_update_raw(
        &model, true, true, false, 6000U, &output);
    if (!model.stable_link_up || !model.stable_ipv4_ready ||
        model.stable_ready || !output.stable_changed ||
        output.report_disconnected) {
        return 12;
    }

    network_manager_wifi_runtime_model_init(&model);
    network_manager_wifi_runtime_model_on_connect_failed(
        &model, 0U, &output);
    if (1U != model.retry_attempt ||
        NETWORK_MANAGER_WIFI_RETRY_INITIAL_BACKOFF_MS != model.retry_delay_ms ||
        !model.retry_deadline_active || output.connect_retry) {
        return 13;
    }
    const uint32_t first_retry_delay =
        NETWORK_MANAGER_WIFI_RETRY_INITIAL_BACKOFF_MS;
    network_manager_wifi_runtime_model_tick(
        &model, first_retry_delay - 1U, &output);
    if (output.connect_retry) {
        return 14;
    }
    network_manager_wifi_runtime_model_tick(&model, first_retry_delay,
                                            &output);
    if (!output.connect_retry || model.retry_deadline_active) {
        return 15;
    }
    network_manager_wifi_runtime_model_on_connect_failed(
        &model, first_retry_delay, &output);
    if (2U != model.retry_attempt ||
        expected_retry_delay(2U) != model.retry_delay_ms) {
        return 16;
    }
    const uint32_t second_retry_delay = expected_retry_delay(2U);
    network_manager_wifi_runtime_model_tick(
        &model, first_retry_delay + second_retry_delay - 1U, &output);
    if (output.connect_retry) {
        return 17;
    }
    network_manager_wifi_runtime_model_tick(
        &model, first_retry_delay + second_retry_delay, &output);
    if (!output.connect_retry) {
        return 18;
    }

    network_manager_wifi_runtime_model_init(&model);
    uint32_t now_ms = 0U;
    for (uint16_t attempt = 1U;
         attempt <= NETWORK_MANAGER_WIFI_RETRY_LIMIT; ++attempt) {
        const uint32_t delay_ms = expected_retry_delay((uint8_t)attempt);
        network_manager_wifi_runtime_model_on_connect_failed(
            &model, now_ms, &output);
        if ((uint8_t)attempt != model.retry_attempt ||
            delay_ms != model.retry_delay_ms ||
            model.retry_exhausted) {
            return 19;
        }
        now_ms += delay_ms;
        network_manager_wifi_runtime_model_tick(&model, now_ms, &output);
        if (!output.connect_retry) {
            return 20;
        }
    }
    network_manager_wifi_runtime_model_on_connect_failed(
        &model, now_ms, &output);
    if (!model.retry_exhausted || !output.retry_exhausted_changed ||
        (uint8_t)NETWORK_MANAGER_WIFI_RETRY_LIMIT != model.retry_attempt ||
        model.retry_deadline_active) {
        return 21;
    }
    network_manager_wifi_runtime_model_on_connect_failed(
        &model, now_ms, &output);
    if (output.retry_exhausted_changed ||
        (uint8_t)NETWORK_MANAGER_WIFI_RETRY_LIMIT != model.retry_attempt) {
        return 22;
    }
    network_manager_wifi_runtime_model_reset_retry(&model);
    if (0U != model.retry_attempt || model.retry_exhausted ||
        model.retry_deadline_active) {
        return 23;
    }
    network_manager_wifi_runtime_model_on_connect_failed(
        &model, now_ms, &output);
    network_manager_wifi_runtime_model_set_automatic_recovery(&model, false);
    if (model.automatic_recovery_enabled || model.retry_deadline_active ||
        model.connect_stability_active || model.disconnect_stability_active) {
        return 24;
    }
    network_manager_wifi_runtime_model_on_connect_failed(
        &model, now_ms, &output);
    if (output.connect_retry || model.retry_deadline_active ||
        1U != model.retry_attempt) {
        return 25;
    }

    /* Bootstrap deadlock guard (design.md:675 vs :406): the stability window
       must arm and complete on link/IP alone, without waiting for internet.
       Otherwise Wi-Fi can never report CONNECTED, net_mgmt never probes the
       Wi-Fi netif, internet never turns true, and the window never arms. */
    network_manager_wifi_runtime_model_init(&model);
    network_manager_wifi_runtime_model_update_raw(
        &model, true, true, false, 0U, &output);
    if (!model.connect_stability_active) {
        return 26; /* window must arm on link/IP even when internet is false */
    }
    network_manager_wifi_runtime_model_tick(&model, 10000U, &output);
    if (!model.stable_link_up || !model.stable_ipv4_ready ||
        model.stable_ready || !output.report_connected) {
        return 27; /* completes: report CONNECTED, but stable_ready stays false */
    }
    /* net_mgmt now sees Wi-Fi, probes it, and internet turns true later. */
    network_manager_wifi_runtime_model_update_raw(
        &model, true, true, true, 12000U, &output);
    if (!model.stable_ready || !output.stable_changed) {
        return 28; /* stable_ready promotes on internet without a new window */
    }
    /* No spurious re-arm or duplicate CONNECTED after link/IP already latched. */
    network_manager_wifi_runtime_model_update_raw(
        &model, true, true, true, 13000U, &output);
    if (model.connect_stability_active || output.report_connected) {
        return 29;
    }
    return 0;
}
