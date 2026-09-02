#include "network_manager_tuning.h"

#if NETWORK_MANAGER_INITIAL_ATTEMPT_TIMEOUT_MS != 17
#error "initial attempt timeout Kconfig mapping failed"
#endif
#if NETWORK_MANAGER_WIFI_CONNECT_STABLE_MS != 19
#error "Wi-Fi connect stability Kconfig mapping failed"
#endif
#if NETWORK_MANAGER_WIFI_DISCONNECT_STABLE_MS != 23
#error "Wi-Fi disconnect stability Kconfig mapping failed"
#endif
#if NETWORK_MANAGER_WIFI_RETRY_LIMIT != 3
#error "Wi-Fi retry limit Kconfig mapping failed"
#endif
#if NETWORK_MANAGER_WIFI_RETRY_UNLIMITED != 1
#error "Wi-Fi unlimited retry Kconfig mapping failed"
#endif
#if NETWORK_MANAGER_WIFI_RETRY_INITIAL_BACKOFF_MS != 29
#error "Wi-Fi initial backoff Kconfig mapping failed"
#endif
#if NETWORK_MANAGER_WIFI_RETRY_MAX_BACKOFF_MS != 31
#error "Wi-Fi maximum backoff Kconfig mapping failed"
#endif
#if NETWORK_MANAGER_CELLULAR_INITIAL_IPV4_WAIT_MS != 37
#error "cellular initial IPv4 wait Kconfig mapping failed"
#endif
#if NETWORK_MANAGER_CELLULAR_POWER_OFF_HOLD_MS != 43
#error "cellular power-off hold Kconfig mapping failed"
#endif
#if defined(NETWORK_MANAGER_CELLULAR_DISCONNECT_GRACE_MS) || \
    defined(NETWORK_MANAGER_CELLULAR_RECOVERY_IPV4_WAIT_MS) || \
    defined(NETWORK_MANAGER_CELLULAR_RETRY_LIMIT) || \
    defined(NETWORK_MANAGER_CELLULAR_RETRY_INITIAL_BACKOFF_MS) || \
    defined(NETWORK_MANAGER_CELLULAR_RETRY_MAX_BACKOFF_MS) || \
    defined(NETWORK_MANAGER_CELLULAR_SELF_CHECK_INTERVAL_MS) || \
    defined(NETWORK_MANAGER_CELLULAR_SELF_CHECK_ENABLED)
#error "retired cellular recovery tuning must not be exposed"
#endif

int main(void)
{
    return 0;
}
