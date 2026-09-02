cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32s3")
set(EXTRA_COMPONENT_DIRS "../../components")
set(PROJECT_VER "1.0.0")
set(CMAKE_DISABLE_FIND_PACKAGE_Git TRUE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(holocubic_demo)

idf_build_get_property(python PYTHON)
set(holo_frame_input_dir "${CMAKE_CURRENT_SOURCE_DIR}/assets/animation_frames")
set(holo_frame_image "${CMAKE_BINARY_DIR}/holo_frames.bin")
set(holo_frame_generator
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_frame_image.py")
file(GLOB holo_frame_inputs CONFIGURE_DEPENDS
     "${holo_frame_input_dir}/*.rgb565")
add_custom_command(
    OUTPUT "${holo_frame_image}"
    COMMAND "${python}" "${holo_frame_generator}"
            --input-dir "${holo_frame_input_dir}"
            --output "${holo_frame_image}"
    DEPENDS "${holo_frame_generator}" ${holo_frame_inputs}
    VERBATIM
)
add_custom_target(holo_frames_image ALL DEPENDS "${holo_frame_image}")
esptool_py_flash_to_partition(flash holo_frames "${holo_frame_image}")
add_dependencies(flash holo_frames_image)
