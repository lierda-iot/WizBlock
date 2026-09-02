idf_component_register(
    SRCS
        "xiaozhi_agent.c"
        "xiaozhi_agent_listen_mode_policy.c"
        "xiaozhi_agent_ws_start_policy.c"
        "xiaozhi_agent_tts_barrier_policy.c"
        "xiaozhi_agent_vad_stop_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp_http_client
        esp_websocket_client
        json
        esp_event
        freertos
        log
        heap
        mbedtls
)
