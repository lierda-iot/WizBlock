@echo off
REM Flash Tank firmware to COM7
REM 烧录坦克固件到 COM7

set ESPTOOL=D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\esptool.py
set PYTHON=D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe
set PORT=COM7
set BAUD=921600

echo ========================================
echo Flashing RC Tank Demo - TANK Role to %PORT%
echo ========================================

REM 全擦除（可选，首次烧录或切换角色时启用）
REM %PYTHON% %ESPTOOL% --chip esp32s3 --port %PORT% erase_flash

echo.
echo Step 1/3: Flashing bootloader...
%PYTHON% %ESPTOOL% --chip esp32s3 --port %PORT% --baud %BAUD% ^
  --before default_reset --after no_reset write_flash ^
  --flash_mode dio --flash_freq 80m --flash_size 8MB ^
  0x0 firmware_backup\bootloader_TANK.bin

if %ERRORLEVEL% NEQ 0 goto error

echo.
echo Step 2/3: Flashing partition table...
%PYTHON% %ESPTOOL% --chip esp32s3 --port %PORT% --baud %BAUD% ^
  --before default_reset --after no_reset write_flash ^
  --flash_mode dio --flash_freq 80m --flash_size 8MB ^
  0x8000 firmware_backup\partition-table_TANK.bin

if %ERRORLEVEL% NEQ 0 goto error

echo.
echo Step 3/3: Flashing application...
%PYTHON% %ESPTOOL% --chip esp32s3 --port %PORT% --baud %BAUD% ^
  --before default_reset --after hard_reset write_flash ^
  --flash_mode dio --flash_freq 80m --flash_size 8MB ^
  0x10000 firmware_backup\rc_tank_demo_TANK.bin

if %ERRORLEVEL% NEQ 0 goto error

echo.
echo ========================================
echo Tank firmware flashed successfully!
echo ========================================
echo Device will reset and start...
goto end

:error
echo.
echo ========================================
echo Flashing FAILED!
echo ========================================

:end
pause
