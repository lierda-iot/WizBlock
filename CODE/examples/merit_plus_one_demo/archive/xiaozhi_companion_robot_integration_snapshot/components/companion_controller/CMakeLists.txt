idf_component_register(
    SRCS "companion_controller_model.c" "companion_controller.c"
    INCLUDE_DIRS "include"
    REQUIRES
        companion_agent_adapter
        companion_audio
        companion_doa
        companion_logic
        companion_motion
        companion_merit_tap
        companion_network
        companion_ui
        esp_common
        esp_timer
        freertos
        log
)
