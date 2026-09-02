cmake_minimum_required(VERSION 3.16)

function(salary_find_cached_component out_var)
    set(_salary_component_name "${ARGV1}")
    set(_salary_component_patterns ${ARGN})
    set(_salary_cache_globs)
    set(_salary_matches)
    set(_salary_result "")

    if(DEFINED ENV{HOME})
        foreach(_salary_pattern IN LISTS _salary_component_patterns)
            list(APPEND _salary_cache_globs
                "$ENV{HOME}/Library/Caches/Espressif/ComponentManager/service_*/${_salary_pattern}"
                "$ENV{HOME}/.cache/Espressif/ComponentManager/service_*/${_salary_pattern}"
            )
        endforeach()
    endif()

    if(DEFINED ENV{LOCALAPPDATA})
        file(TO_CMAKE_PATH "$ENV{LOCALAPPDATA}" _salary_localappdata)
        foreach(_salary_pattern IN LISTS _salary_component_patterns)
            list(APPEND _salary_cache_globs
                "${_salary_localappdata}/Espressif/ComponentManager/service_*/${_salary_pattern}"
            )
        endforeach()
    endif()

    foreach(_salary_cache_glob IN LISTS _salary_cache_globs)
        file(GLOB _salary_matches LIST_DIRECTORIES true "${_salary_cache_glob}")
        if(_salary_matches)
            list(SORT _salary_matches)
            list(REVERSE _salary_matches)
            list(GET _salary_matches 0 _salary_result)
            break()
        endif()
    endforeach()

    if(_salary_result)
        message(STATUS "salary_calculator_demo: reuse local ${_salary_component_name} cache at ${_salary_result}")
    endif()

    set(${out_var} "${_salary_result}" PARENT_SCOPE)
endfunction()

set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/aip8563_rtc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/bmi260_imu"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/board_adc"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/board_power"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/bus_i2c"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/bus_spi"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/camera_hal"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/capability_registry"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/display_hal"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/io_expander"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/laiwfs300"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/launcher_return"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/module_detect"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/pt2466_motor"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/robot_motion"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../components/touch_hal"
)

salary_find_cached_component(_salary_lvgl_component_dir "lvgl" "lvgl__lvgl_8.4.0_*")
salary_find_cached_component(_salary_esp_codec_component_dir "esp_codec_dev" "espressif__esp_codec_dev_1.5.*_*" "espressif__esp_codec_dev_*")

if(NOT _salary_lvgl_component_dir)
    message(FATAL_ERROR
        "salary_calculator_demo: local LVGL 8.4.0 cache not found. "
        "Please build lvgl_demo once or restore the ESP ComponentManager cache, then retry.")
endif()

if(NOT _salary_esp_codec_component_dir)
    message(FATAL_ERROR
        "salary_calculator_demo: local esp_codec_dev cache not found. "
        "Please build an audio example once or restore the ESP ComponentManager cache, then retry.")
endif()

set(_salary_cache_stage_dir "${CMAKE_CURRENT_BINARY_DIR}/cached_components")
file(REMOVE_RECURSE "${_salary_cache_stage_dir}")
file(MAKE_DIRECTORY "${_salary_cache_stage_dir}")
file(CREATE_LINK "${_salary_lvgl_component_dir}" "${_salary_cache_stage_dir}/lvgl" SYMBOLIC COPY_ON_ERROR)
file(CREATE_LINK "${_salary_esp_codec_component_dir}" "${_salary_cache_stage_dir}/esp_codec_dev" SYMBOLIC COPY_ON_ERROR)

list(APPEND EXTRA_COMPONENT_DIRS
    "${_salary_cache_stage_dir}"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(salary_calculator_demo)
