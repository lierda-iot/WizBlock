idf_component_register(
    SRCS
        "app_main.c"
        "app_startup.c"
        "app_capability_router.c"
    INCLUDE_DIRS "."
    REQUIRES
        laiwfs300
        bringup_test
        capability_registry
        module_detect
)
