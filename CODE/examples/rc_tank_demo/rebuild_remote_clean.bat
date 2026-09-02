@echo off
REM Clean rebuild Remote firmware for RC Tank Demo (EX-035)

set IDF_PATH=D:\Espressif\frameworks\esp-idf-v5.5.4
set IDF_TOOLS_PATH=D:\Espressif
set PATH=D:\Espressif\tools\cmake\3.24.0\bin;D:\Espressif\tools\ninja\1.11.1;D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;D:\Espressif\python_env\idf5.5_py3.11_env\Scripts;%PATH%
set SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.defaults.remote

echo ========================================
echo Clean Rebuild Remote firmware
echo ========================================

D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe %IDF_PATH%\tools\idf.py set-target esp32s3
if %ERRORLEVEL% NEQ 0 exit /b 1

D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe %IDF_PATH%\tools\idf.py build
if %ERRORLEVEL% NEQ 0 exit /b 1

echo ========================================
echo Backup firmware
echo ========================================
if not exist firmware_backup mkdir firmware_backup
copy /Y build\rc_tank_demo.bin firmware_backup\rc_tank_demo_REMOTE.bin
copy /Y build\bootloader\bootloader.bin firmware_backup\bootloader_REMOTE.bin
copy /Y build\partition_table\partition-table.bin firmware_backup\partition-table_REMOTE.bin

echo ========================================
echo Remote firmware rebuilt successfully!
echo ========================================
