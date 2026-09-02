set(app_sources "xiaozhi_companion_robot_demo_main.c")
set(app_include_dirs ".")

if(CONFIG_XIAOZHI_COMPANION_SELF_TEST)
    list(APPEND app_sources "../tests/companion_core_test_cases.c")
    list(APPEND app_include_dirs "../tests")
endif()

idf_component_register(
    SRCS ${app_sources}
    INCLUDE_DIRS ${app_include_dirs}
    REQUIRES
        companion_core
        companion_logic
        companion_motion
        companion_input
        companion_ui
        companion_touch_gesture
        companion_audio
        companion_doa
        companion_controller
        companion_agent_adapter
        network_manager
        xiaozhi_agent
        audio_processor
        laiwfs300
        lte_hal
        net_mgmt
        nvs_flash
        esp_event
        esp_netif
        esp_wifi
        spiffs
        robot_motion
        pt2466_motor
        board_adc
        display_hal
        touch_hal
        io_expander
    PRIV_REQUIRES
        lvgl
)

spiffs_create_partition_image(spiffs_data ../spiffs FLASH_IN_PROJECT)
