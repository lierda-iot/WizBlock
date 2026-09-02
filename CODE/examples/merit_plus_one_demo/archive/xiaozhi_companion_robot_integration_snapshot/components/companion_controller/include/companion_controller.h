#pragma once

#include "companion_agent_adapter.h"
#include "companion_audio.h"
#include "companion_controller_model.h"
#include "companion_doa.h"
#include "companion_logic.h"
#include "companion_motion.h"
#include "companion_network.h"
#include "esp_err.h"

#include <stdbool.h>
#include <stdint.h>

typedef struct {
    companion_roam_config_t roam_config;
} companion_controller_config_t;

typedef struct {
    companion_product_state_t product_state;
    uint32_t generation;
    uint32_t wake_seq;
    uint32_t events_processed;
    uint32_t queue_drops;
    uint32_t stale_events;
    uint32_t wakes_accepted;
    uint32_t wakes_rejected;
    uint32_t upload_frames;
    uint32_t upload_drops;
    uint32_t module_errors;
    uint32_t queue_peak;
    bool roam_enabled;
    bool network_ready;
    bool network_link_up;
    bool network_ipv4_ready;
    bool network_internet_reachable;
    bool upload_gate_open;
    companion_network_lifecycle_t network_lifecycle;
    companion_network_interface_t network_interface;
    companion_network_phase_t network_phase;
    uint32_t network_revision;
    uint32_t network_recovery_attempt;
    esp_err_t network_error;
} companion_controller_stats_t;

void companion_controller_config_default(companion_controller_config_t *config);
esp_err_t companion_controller_start(const companion_controller_config_t *config);
esp_err_t companion_controller_finish_startup(void);
esp_err_t companion_controller_set_capability(companion_capability_t capability,
                                               bool available,
                                               esp_err_t error);
void companion_controller_on_network(bool ready, const char *interface_name,
                                     void *user_ctx);
void companion_controller_on_network_snapshot(
    const companion_network_snapshot_t *snapshot, void *user_ctx);
esp_err_t companion_controller_reserve_wake(uint32_t *generation,
                                            uint32_t *wake_seq,
                                            void *user_ctx);
void companion_controller_on_audio_event(const companion_audio_event_t *event,
                                         void *user_ctx);
void companion_controller_on_opus(const uint8_t *data, int length,
                                  void *user_ctx);
void companion_controller_on_doa(const companion_doa_result_t *result,
                                 void *user_ctx);
void companion_controller_on_motion_done(
    const companion_motion_command_t *command,
    const companion_motion_result_t *result,
    void *user_ctx);
void companion_controller_on_motion_progress(
    const companion_motion_command_t *command,
    const companion_motion_progress_t *progress,
    void *user_ctx);
void companion_controller_on_merit_tap(
    const companion_merit_result_t *result, uint32_t generation,
    uint32_t wake_seq, uint64_t timestamp_us, void *user_ctx);
void companion_controller_on_agent_event(const companion_agent_event_t *event,
                                         void *user_ctx);
void companion_controller_on_agent_audio_event(
    const companion_agent_audio_event_t *event, void *user_ctx);
void companion_controller_on_sw3_click(void *user_ctx);
void companion_controller_on_input_error(esp_err_t error, void *user_ctx);
void companion_controller_on_touch(bool pressed, void *user_ctx);
void companion_controller_get_stats(companion_controller_stats_t *stats);
