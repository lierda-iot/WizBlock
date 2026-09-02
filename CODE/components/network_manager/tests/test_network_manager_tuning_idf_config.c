#include "network_manager_tuning.h"

#if NETWORK_MANAGER_WIFI_CONNECT_STABLE_MS != 2000U
#error "ESP-IDF sdkconfig.h was not consumed for Wi-Fi stability"
#endif
#if NETWORK_MANAGER_WIFI_DISCONNECT_STABLE_MS != 500U
#error "ESP-IDF sdkconfig.h was not consumed for Wi-Fi disconnect stability"
#endif
#if NETWORK_MANAGER_WIFI_RETRY_LIMIT != 255U
#error "ESP-IDF sdkconfig.h was not consumed for Wi-Fi retry limit"
#endif
#if NETWORK_MANAGER_WIFI_RETRY_UNLIMITED != 1
#error "ESP-IDF sdkconfig.h was not consumed for unlimited Wi-Fi retry"
#endif
#if NETWORK_MANAGER_WIFI_RETRY_INITIAL_BACKOFF_MS != 250U
#error "ESP-IDF sdkconfig.h was not consumed for Wi-Fi initial backoff"
#endif
#if NETWORK_MANAGER_WIFI_RETRY_MAX_BACKOFF_MS != 500U
#error "ESP-IDF sdkconfig.h was not consumed for Wi-Fi maximum backoff"
#endif

int main(void)
{
    return 0;
}
