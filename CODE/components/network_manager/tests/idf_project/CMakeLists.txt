cmake_minimum_required(VERSION 3.16)

set(EXTRA_COMPONENT_DIRS "${CMAKE_CURRENT_LIST_DIR}/../..")
set(MINIMAL_BUILD ON)
set(COMPONENTS
    main
    network_manager
    lte_hal
    net_mgmt
    nvs_flash
    esp_event
    esp_eth
    esp_netif
    esp_wifi
    freertos
    log
    esp_common
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(network_manager_component_test)
