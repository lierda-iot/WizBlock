/*
 * Pure passive cellular runtime model tests.
 *
 * Cellular state is observation-only. Link/IP transitions come from reported
 * facts, and elapsed time must never synthesize recovery work.
 */
#include "network_manager_cellular_runtime_model.h"

int main(void)
{
    network_manager_cellular_runtime_model_t model;
    network_manager_cellular_runtime_output_t output;

    /* Test 1: Initial wait timeout does NOT trigger power cycle. */
    network_manager_cellular_runtime_model_init(&model);
    network_manager_cellular_runtime_model_manager_initialized(
        &model, 100U, &output);

    /* Before timeout: should stay in initial_wait, no effect. */
    network_manager_cellular_runtime_model_tick(&model, 20099U, &output);
    if (output.initial_ipv4_timeout) {
        return 1;
    }

    /* At timeout: should signal timeout but remain passive (no effect). */
    network_manager_cellular_runtime_model_tick(&model, 20100U, &output);
    if (!output.initial_ipv4_timeout ||
        NETWORK_MANAGER_4G_WAIT_LINK != model.phase) {
        return 2;
    }

    /* Test 2: Link and IPv4 arrive → model tracks ready state. */
    network_manager_cellular_runtime_model_update_raw(
        &model, true, true, false, 25000U, &output);
    if (!model.ever_ipv4_ready || !model.raw_ipv4_ready ||
        NETWORK_MANAGER_4G_WAIT_INTERNET != model.phase ||
        model.initial_wait_active) {
        return 3;
    }

    /* Test 3: Link drops -> wait for a new reported link fact immediately. */
    network_manager_cellular_runtime_model_update_raw(
        &model, false, false, false, 30000U, &output);
    if (NETWORK_MANAGER_4G_WAIT_LINK != model.phase) {
        return 4;
    }

    /* Test 4: Long elapsed time never creates grace, retries, or LTE effects. */
    network_manager_cellular_runtime_model_tick(&model, 0xF0000000U, &output);
    if (NETWORK_MANAGER_4G_WAIT_LINK != model.phase) {
        return 5;
    }

    /* Test 5: Link recovery → model returns to ready. */
    network_manager_cellular_runtime_model_update_raw(
        &model, true, true, true, 40000U, &output);
    if (!output.recovered || !model.raw_ipv4_ready ||
        NETWORK_MANAGER_4G_READY != model.phase) {
        return 6;
    }

    /* Test 6: A subsequent disconnect remains observation-only. */
    network_manager_cellular_runtime_model_update_raw(
        &model, false, false, false, 50000U, &output);
    if (NETWORK_MANAGER_4G_WAIT_LINK != model.phase) {
        return 7;
    }

    /* A new reported ready fact restores READY. */
    network_manager_cellular_runtime_model_update_raw(
        &model, true, true, true, 52000U, &output);
    if (NETWORK_MANAGER_4G_READY != model.phase) {
        return 8;
    }

    /* Test 7: First-time link (never had IPv4) → no grace period. */
    network_manager_cellular_runtime_model_init(&model);
    network_manager_cellular_runtime_model_manager_initialized(
        &model, 0U, &output);

    /* Link arrives but no IPv4 yet. */
    network_manager_cellular_runtime_model_update_raw(
        &model, true, false, false, 100U, &output);
    if (model.ever_ipv4_ready ||
        NETWORK_MANAGER_4G_WAIT_IPV4 != model.phase) {
        return 9;
    }

    /* Link drops → should stay in WAIT_LINK without grace (never had IPv4). */
    network_manager_cellular_runtime_model_update_raw(
        &model, false, false, false, 200U, &output);
    if (NETWORK_MANAGER_4G_WAIT_LINK != model.phase) {
        return 10;
    }

    /* Test 8: IPv4 ready with internet → READY phase. */
    network_manager_cellular_runtime_model_init(&model);
    network_manager_cellular_runtime_model_manager_initialized(
        &model, 0U, &output);
    network_manager_cellular_runtime_model_update_raw(
        &model, true, true, true, 1000U, &output);
    if (NETWORK_MANAGER_4G_READY != model.phase ||
        !model.internet_reachable) {
        return 11;
    }

    /* IPv4 persists but internet drops → WAIT_INTERNET phase. */
    network_manager_cellular_runtime_model_update_raw(
        &model, true, true, false, 2000U, &output);
    if (NETWORK_MANAGER_4G_WAIT_INTERNET != model.phase) {
        return 12;
    }

    /* Internet returns → back to READY. */
    network_manager_cellular_runtime_model_update_raw(
        &model, true, true, true, 3000U, &output);
    if (NETWORK_MANAGER_4G_READY != model.phase) {
        return 13;
    }

    /* Test 9: Repeated observation sequences remain passive. */
    network_manager_cellular_runtime_model_init(&model);
    network_manager_cellular_runtime_model_manager_initialized(
        &model, 0U, &output);
    network_manager_cellular_runtime_model_update_raw(
        &model, true, true, true, 100U, &output);
    network_manager_cellular_runtime_model_update_raw(
        &model, false, false, false, 200U, &output);

    if (NETWORK_MANAGER_4G_WAIT_LINK != model.phase) {
        return 14;
    }

    return 0;  /* All tests pass */
}
