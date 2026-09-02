idf_component_register(
    SRCS "xiaozhi_ai_demo_main.c"
    INCLUDE_DIRS "."
    REQUIRES
        xiaozhi_agent
        xiaozhi_audio
        laiwfs300
        lte_hal
        net_mgmt
        nvs_flash
        esp_event
        esp_netif
        esp_wifi
        spiffs
)

spiffs_create_partition_image(spiffs_data ../spiffs FLASH_IN_PROJECT)
