idf_component_register(
    SRCS
        "salary_calculator_demo_main.c"
        "salary_calculator_logic.c"
        "salary_calculator_ui.c"
    INCLUDE_DIRS "."
    EMBED_FILES
        "money_background.rgb565"
    REQUIRES
        aip8563_rtc
        display_hal
        launcher_return
        laiwfs300
        nvs_flash
        spiffs
        touch_hal
    PRIV_REQUIRES
        esp_timer
        lvgl
)

spiffs_create_partition_image(spiffs_data ../spiffs FLASH_IN_PROJECT)
