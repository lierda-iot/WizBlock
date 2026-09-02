idf_component_register(
    SRCS "companion_agent_adapter.c"
         "companion_agent_binding_policy.c"
    INCLUDE_DIRS "include"
    REQUIRES companion_core xiaozhi_agent esp_common log
)
