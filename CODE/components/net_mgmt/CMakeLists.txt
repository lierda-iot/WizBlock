set(NET_MGMT_DEPS
    esp_common
    esp_wifi
    esp_netif
    esp_event
    lwip
    esp_timer
    log
    espressif__iot_usbh_ecm
)

if(IDF_VERSION_MAJOR GREATER_EQUAL 6)
    # v6.0 及以上版本使用 Component Manager 中的 usb
    list(APPEND NET_MGMT_DEPS "espressif__usb")
else()
    # v5.5 及以下版本使用内置的 usb 组件
    list(APPEND NET_MGMT_DEPS "usb")
endif()

set(NET_MGMT_IDF_VERSION_TAG "idf${IDF_VERSION_MAJOR}_${IDF_VERSION_MINOR}_${IDF_VERSION_PATCH}")
set(NET_MGMT_LIB_CANDIDATES
    "${CMAKE_CURRENT_LIST_DIR}/lib/libnet_mgmt_${IDF_TARGET}_${NET_MGMT_IDF_VERSION_TAG}.a"
)

set(NET_MGMT_PREBUILT_LIB "")
foreach(candidate IN LISTS NET_MGMT_LIB_CANDIDATES)
    if(EXISTS "${candidate}")
        set(NET_MGMT_PREBUILT_LIB "${candidate}")
        break()
    endif()
endforeach()

if(NOT NET_MGMT_PREBUILT_LIB)
    message(FATAL_ERROR
        "net_mgmt prebuilt library not found. Tried: ${NET_MGMT_LIB_CANDIDATES}")
endif()

idf_component_register(
    INCLUDE_DIRS "include"
    REQUIRES ${NET_MGMT_DEPS}
)

add_prebuilt_library(net_mgmt
    "${NET_MGMT_PREBUILT_LIB}"
    REQUIRES ${NET_MGMT_DEPS}
)

target_link_libraries(${COMPONENT_LIB} INTERFACE net_mgmt)
