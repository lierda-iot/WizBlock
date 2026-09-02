cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32s3")
set(EXTRA_COMPONENT_DIRS "../../components")
set(PROJECT_VER "1.0.0")
set(CMAKE_DISABLE_FIND_PACKAGE_Git TRUE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(audio_ofdm_text_demo)
