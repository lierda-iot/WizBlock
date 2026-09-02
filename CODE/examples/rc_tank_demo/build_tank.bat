@echo off
REM Build Tank firmware for RC Tank Demo (EX-035)
REM 构建坦克固件

set IDF_PATH=D:\Espressif\frameworks\esp-idf-v5.5.4
set IDF_TOOLS_PATH=D:\Espressif
set PATH=D:\Espressif\tools\cmake\3.24.0\bin;D:\Espressif\tools\ninja\1.11.1;D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;D:\Espressif\tools\esp32ulp-elf\2.35_20220830\esp32ulp-elf\bin;D:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20240318\openocd-esp32\bin;D:\Espressif\tools\riscv32-esp-elf\esp-13.2.0_20240530\riscv32-esp-elf\bin;D:\Espressif\python_env\idf5.5_py3.11_env\Scripts;%PATH%

echo ========================================
echo Building RC Tank Demo - TANK Role
echo ========================================

REM Set Tank role
set SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.tank

REM Clean and build
D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe %IDF_PATH%\tools\idf.py fullclean
D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe %IDF_PATH%\tools\idf.py set-target esp32s3
D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe %IDF_PATH%\tools\idf.py build

if %ERRORLEVEL% EQU 0 (
    echo ========================================
    echo Tank firmware built successfully!
    echo ========================================

    REM Backup firmware
    if not exist firmware_backup mkdir firmware_backup
    copy /Y build\rc_tank_demo.bin firmware_backup\rc_tank_demo_TANK.bin
    copy /Y build\bootloader\bootloader.bin firmware_backup\bootloader_TANK.bin
    copy /Y build\partition_table\partition-table.bin firmware_backup\partition-table_TANK.bin

    echo Firmware backed up to firmware_backup\rc_tank_demo_TANK.bin
) else (
    echo ========================================
    echo Tank firmware build FAILED!
    echo ========================================
)

pause
