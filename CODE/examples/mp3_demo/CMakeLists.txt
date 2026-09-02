cmake_minimum_required(VERSION 3.16)

set(IDF_TARGET "esp32s3")
set(EXTRA_COMPONENT_DIRS
    "../../components/aip8563_rtc"
    "../../components/bmi260_imu"
    "../../components/board_adc"
    "../../components/board_power"
    "../../components/bus_i2c"
    "../../components/bus_spi"
    "../../components/camera_hal"
    "../../components/capability_registry"
    "../../components/display_hal"
    "../../components/io_expander"
    "../../components/laiwfs300"
    "../../components/module_detect"
    "../../components/pt2466_motor"
    "../../components/robot_motion"
    "../../components/storage_hal"
    "../../components/touch_hal"
)
set(PROJECT_VER "1.0.0")
# The macOS builder mirrors this project without repository metadata.
set(CMAKE_DISABLE_FIND_PACKAGE_Git TRUE)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(mp3_demo)
