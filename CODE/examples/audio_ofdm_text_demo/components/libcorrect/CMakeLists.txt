idf_component_register(
    SRCS
        "src/reed-solomon/decode.c"
        "src/reed-solomon/encode.c"
        "src/reed-solomon/polynomial.c"
        "src/reed-solomon/reed-solomon.c"
    INCLUDE_DIRS "include"
)

# The upstream files are vendored without algorithm changes. Keep their
# implementation warnings local to this private component.
target_compile_options(${COMPONENT_LIB} PRIVATE
    -Wno-unused-function
    -Wno-unused-parameter
)
