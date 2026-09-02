idf_component_register(
    SRCS "src/kiss_fft.c"
    INCLUDE_DIRS "include"
)

target_compile_definitions(${COMPONENT_LIB} PUBLIC kiss_fft_scalar=float)
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-sign-compare
    -Wno-unused-function
    -Wno-unused-parameter
)
