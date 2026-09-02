cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32s3")
set(EXTRA_COMPONENT_DIRS "../../components")
set(SDKCONFIG_DEFAULTS "sdkconfig.defaults")
set(CMAKE_DISABLE_FIND_PACKAGE_Git TRUE CACHE BOOL
    "Disable CMake Git package discovery for this Example" FORCE)
set(GIT_EXECUTABLE "GIT_EXECUTABLE-NOTFOUND" CACHE FILEPATH
    "Disable Git version probing for this Example" FORCE)

if("$ENV{COMPANION_SELF_TEST}" STREQUAL "1")
    list(APPEND SDKCONFIG_DEFAULTS "tests/sdkconfig.self_test")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
idf_build_set_property(EXTRA_CMAKE_ARGS
    "-DCMAKE_DISABLE_FIND_PACKAGE_Git=TRUE" APPEND)
idf_build_set_property(EXTRA_CMAKE_ARGS
    "-DGIT_EXECUTABLE=GIT_EXECUTABLE-NOTFOUND" APPEND)
set(PROJECT_VER "0.2.0-integration")
project(xiaozhi_companion_robot_demo)
