cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32s3")
set(EXTRA_COMPONENT_DIRS "../../components")
set(PROJECT_VER "1.0.0")
# This project is built from an ASCII mirror without repository metadata.
# Disable ESP-IDF's optional Git discovery so builds honor the project Git gate.
set(CMAKE_DISABLE_FIND_PACKAGE_Git TRUE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(touch_2048_demo)
