/*
 * Pure Wi-Fi debounce and retry-budget model.
 *
 * Raw link/IP facts are immediate. Application readiness is published only
 * after configured stability windows, and automatic reconnect uses a bounded
 * build-time retry budget.
 */
#include "network_manager_wifi_runtime_model.h"

#include <stddef.h>

static void clear_output(network_manager_wifi_runtime_output_t *output)
{
    *output = (network_manager_wifi_runtime_output_t){0};
}

static bool elapsed(uint32_t now_ms,
                    uint32_t since_ms,
                    uint32_t duration_ms)
{
    /* Unsigned subtraction keeps deadline checks valid across tick wrap. */
    return (uint32_t)(now_ms - since_ms) >= duration_ms;
}

static uint32_t retry_delay(uint8_t attempt)
{
    /* Double the configured first delay without exceeding the configured cap. */
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

void network_manager_wifi_runtime_model_init(
    network_manager_wifi_runtime_model_t *model)
{
    if (NULL == model) {
        return;
    }
    *model = (network_manager_wifi_runtime_model_t){
        .automatic_recovery_enabled = true,
    };
}

void network_manager_wifi_runtime_model_update_raw(
    network_manager_wifi_runtime_model_t *model,
    bool link_up,
    bool ipv4_ready,
    bool internet_reachable,
    uint32_t now_ms,
    network_manager_wifi_runtime_output_t *output)
{
    if (NULL == model || NULL == output) {
        return;
    }
    clear_output(output);
    const bool was_raw_available =
        model->raw_link_up && model->raw_ipv4_ready;
    model->raw_link_up = link_up;
    model->raw_ipv4_ready = ipv4_ready;
    model->internet_reachable = internet_reachable;
    const bool raw_available = link_up && ipv4_ready;

    /* Internet loss clears ready without pretending that link or IPv4 fell. */
    if (raw_available && !internet_reachable && model->stable_ready) {
        model->stable_ready = false;
        output->stable_changed = true;
    }

    /* Promote stable_ready when internet arrives (link/IP already stable). */
    if (model->stable_link_up && model->stable_ipv4_ready &&
        internet_reachable && !model->stable_ready) {
        model->stable_ready = true;
        output->stable_changed = true;
    }

    if (was_raw_available && !raw_available) {
        output->report_disconnected = true;
    }
    /* Link loss must remain continuous for the disconnect stability window. */
    if (model->stable_link_up && model->stable_ipv4_ready &&
        !raw_available && !model->disconnect_stability_active) {
        model->disconnect_stability_active = true;
        model->disconnect_stability_since_ms = now_ms;
    } else if (raw_available) {
        model->disconnect_stability_active = false;
    }
    /* A consumed retry budget resets only after a full stable-connect window. */
    if (raw_available && model->automatic_recovery_enabled &&
        0U != model->retry_attempt &&
        !model->retry_reset_stability_active) {
        model->retry_reset_stability_active = true;
        model->retry_reset_stability_since_ms = now_ms;
    } else if (!raw_available) {
        model->retry_reset_stability_active = false;
    }

    if (raw_available &&
        !model->stable_link_up && !model->connect_stability_active) {
        model->connect_stability_active = true;
        model->connect_stability_since_ms = now_ms;
    } else if (!link_up || !ipv4_ready) {
        model->connect_stability_active = false;
    }
}

void network_manager_wifi_runtime_model_tick(
    network_manager_wifi_runtime_model_t *model,
    uint32_t now_ms,
    network_manager_wifi_runtime_output_t *output)
{
    if (NULL == model || NULL == output) {
        return;
    }
    clear_output(output);
    if (model->retry_deadline_active &&
        elapsed(now_ms,
                model->retry_deadline_since_ms,
                model->retry_delay_ms)) {
        model->retry_deadline_active = false;
        output->connect_retry = true;
    }
    if (model->connect_stability_active &&
        elapsed(now_ms,
                model->connect_stability_since_ms,
                NETWORK_MANAGER_WIFI_CONNECT_STABLE_MS)) {
        model->connect_stability_active = false;
        model->stable_link_up = model->raw_link_up;
        model->stable_ipv4_ready = model->raw_ipv4_ready;
        model->stable_ready = model->raw_link_up &&
                              model->raw_ipv4_ready &&
                              model->internet_reachable;
        output->stable_changed = model->stable_ready;
        output->report_connected = true;
    }
    if (model->disconnect_stability_active &&
        elapsed(now_ms,
                model->disconnect_stability_since_ms,
                NETWORK_MANAGER_WIFI_DISCONNECT_STABLE_MS)) {
        model->disconnect_stability_active = false;
        model->stable_link_up = false;
        model->stable_ipv4_ready = false;
        model->stable_ready = false;
        output->stable_changed = true;
    }
    if (model->retry_reset_stability_active &&
        elapsed(now_ms,
                model->retry_reset_stability_since_ms,
                NETWORK_MANAGER_WIFI_CONNECT_STABLE_MS)) {
        model->retry_reset_stability_active = false;
        model->retry_attempt = 0U;
        model->retry_exhausted = false;
        output->retry_budget_reset = true;
    }
}

void network_manager_wifi_runtime_model_on_connect_failed(
    network_manager_wifi_runtime_model_t *model,
    uint32_t now_ms,
    network_manager_wifi_runtime_output_t *output)
{
    if (NULL == model || NULL == output) {
        return;
    }
    clear_output(output);
    if (!model->automatic_recovery_enabled ||
        (!NETWORK_MANAGER_WIFI_RETRY_UNLIMITED && model->retry_exhausted)) {
        return;
    }
#if NETWORK_MANAGER_WIFI_RETRY_UNLIMITED
    /* The public snapshot keeps an 8-bit attempt count; saturate it while
       continuing to schedule retries indefinitely for opted-in builds. */
    if (UINT8_MAX > model->retry_attempt) {
        ++model->retry_attempt;
    }
    model->retry_delay_ms = retry_delay(model->retry_attempt);
    model->retry_deadline_since_ms = now_ms;
    model->retry_deadline_active = true;
    return;
#else
    if (NETWORK_MANAGER_WIFI_RETRY_LIMIT <= model->retry_attempt) {
        model->retry_exhausted = true;
        model->retry_deadline_active = false;
        output->retry_exhausted_changed = true;
        return;
    }

    /* The first application connect is external; this counts recovery only. */
    ++model->retry_attempt;
    model->retry_delay_ms = retry_delay(model->retry_attempt);
    model->retry_deadline_since_ms = now_ms;
    model->retry_deadline_active = true;
#endif
}

void network_manager_wifi_runtime_model_reset_retry(
    network_manager_wifi_runtime_model_t *model)
{
    if (NULL == model) {
        return;
    }
    model->retry_attempt = 0U;
    model->retry_exhausted = false;
    model->retry_deadline_active = false;
    model->retry_delay_ms = 0U;
    model->retry_reset_stability_active = false;
}

void network_manager_wifi_runtime_model_set_automatic_recovery(
    network_manager_wifi_runtime_model_t *model,
    bool enabled)
{
    if (NULL == model) {
        return;
    }
    model->automatic_recovery_enabled = enabled;
    if (!enabled) {
        /* Manual offline cancels every pending automatic transition. */
        model->connect_stability_active = false;
        model->disconnect_stability_active = false;
        model->retry_reset_stability_active = false;
        model->retry_deadline_active = false;
    }
}
