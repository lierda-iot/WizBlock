idf_component_register(
    SRCS "lte_net_demo_main.c"
          "demo_network.c"
          "demo_ui.c"
          "lte_net_state_view.c"
          "lte_net_wifi_options.c"
    INCLUDE_DIRS "."
    REQUIRES laiwfs300 network_manager display_hal touch_hal
    PRIV_REQUIRES nvs_flash esp_timer lvgl
)
