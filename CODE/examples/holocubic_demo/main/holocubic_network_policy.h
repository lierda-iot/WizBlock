#pragma once

#include <stdbool.h>
#include <stdint.h>

#define HOLO_NETWORK_WEATHER_REFRESH_MS (30ULL * 60ULL * 1000ULL)
#define HOLO_NETWORK_WEATHER_RETRY_MS (60ULL * 1000ULL)

typedef enum {
    HOLO_NETWORK_WAITING = 0,
    HOLO_NETWORK_ONLINE = 1,
    HOLO_NETWORK_OFFLINE = 2,
} holocubic_network_state_t;

typedef struct {
    bool manager_start_failed;
    bool stable_ready;
    bool cellular_active;
} holocubic_network_observation_t;

typedef struct {
    uint64_t next_fetch_ms;
} holocubic_network_schedule_t;

holocubic_network_state_t holocubic_network_decide(
    const holocubic_network_observation_t *observation);
bool holocubic_network_weather_due(
    const holocubic_network_schedule_t *schedule,
    uint64_t now_ms);
void holocubic_network_weather_result(
    holocubic_network_schedule_t *schedule,
    uint64_t now_ms,
    bool success);
