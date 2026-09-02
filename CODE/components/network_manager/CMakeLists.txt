if(CONFIG_NETWORK_MANAGER_ENABLE)
    set(network_manager_sources
        "network_manager.c"
        "network_manager_policy.c"
        "network_manager_wifi_config.c"
        "network_manager_wifi_scan.c"
        "network_manager_event_journal.c"
        "network_manager_storage_model.c"
        "network_manager_storage_nvs.c"
        "network_manager_wifi_runtime_model.c"
        "network_manager_cellular_runtime_model.c"
        "network_manager_dual_runtime_model.c"
    )
    set(network_manager_public_includes "include")
    set(network_manager_private_includes "private_include")
endif()

idf_component_register(
    SRCS ${network_manager_sources}
    INCLUDE_DIRS ${network_manager_public_includes}
    PRIV_INCLUDE_DIRS ${network_manager_private_includes}
)

if(CONFIG_NETWORK_MANAGER_ENABLE)
    target_link_libraries(${COMPONENT_LIB}
        PUBLIC
            idf::esp_common
        PRIVATE
            idf::lte_hal
            idf::net_mgmt
            idf::nvs_flash
            idf::esp_event
            idf::esp_eth
            idf::esp_netif
            idf::esp_wifi
            idf::freertos
            idf::log
    )
    target_link_options(${COMPONENT_LIB}
        INTERFACE
            "LINKER:--wrap=lsd_net_send_event"
    )
endif()
