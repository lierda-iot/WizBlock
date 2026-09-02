file(GLOB GUI_GENERATED_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/../generated/*.c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../generated/guider_fonts/*.c"
)

file(GLOB GUI_CUSTOM_SRCS
    "${CMAKE_CURRENT_SOURCE_DIR}/../custom/*.c"
)

idf_component_register(
    SRCS "lvgl_demo_main.c"
         ${GUI_GENERATED_SRCS}
         ${GUI_CUSTOM_SRCS}
    INCLUDE_DIRS "."
                 "../generated"
                 "../custom"
    REQUIRES laiwfs300 display_hal touch_hal
)
