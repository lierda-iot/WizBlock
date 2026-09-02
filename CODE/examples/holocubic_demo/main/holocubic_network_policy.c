#include "holocubic_network_policy.h"

#include <stdint.h>
#include <stddef.h>

holocubic_network_state_t holocubic_network_decide(
    const holocubic_network_observation_t *observation)
{
    if (NULL == observation || observation->manager_start_failed) {
        return HOLO_NETWORK_OFFLINE;
    }
    if (observation->stable_ready && observation->cellular_active) {
        return HOLO_NETWORK_ONLINE;
    }
    return HOLO_NETWORK_WAITING;
}

bool holocubic_network_weather_due(
    const holocubic_network_schedule_t *schedule,
    uint64_t now_ms)
{
    return NULL != schedule &&
           (0U == schedule->next_fetch_ms ||
            now_ms >= schedule->next_fetch_ms);
}

void holocubic_network_weather_result(
    holocubic_network_schedule_t *schedule,
    uint64_t now_ms,
    bool success)
{
    uint64_t interval_ms = success ? HOLO_NETWORK_WEATHER_REFRESH_MS :
                                     HOLO_NETWORK_WEATHER_RETRY_MS;

    if (NULL == schedule) {
        return;
    }
    schedule->next_fetch_ms =
        interval_ms > UINT64_MAX - now_ms ? UINT64_MAX : now_ms + interval_ms;
}
