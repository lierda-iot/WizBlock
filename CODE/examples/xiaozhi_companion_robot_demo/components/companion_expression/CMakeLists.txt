idf_component_register(
    SRCS
        "companion_expression.c"
        "generated/companion_expression_assets.c"
    INCLUDE_DIRS "include"
    PRIV_INCLUDE_DIRS "."
    REQUIRES companion_core esp_common log
)
