# Build Tank firmware for RC Tank Demo (EX-035)
# PowerShell script to avoid MSys/Mingw detection

$ErrorActionPreference = "Stop"

Set-Location "e:\10__AIProject\7_AI陪伴机器人\CODE\examples\rc_tank_demo"

$env:IDF_PATH = "D:\Espressif\frameworks\esp-idf-v5.5.4"
$env:IDF_TOOLS_PATH = "D:\Espressif"
$env:PATH = "D:\Espressif\tools\cmake\3.24.0\bin;D:\Espressif\tools\ninja\1.11.1;D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;D:\Espressif\tools\esp32ulp-elf\2.35_20220830\esp32ulp-elf\bin;D:\Espressif\tools\openocd-esp32\v0.12.0-esp32-20240318\openocd-esp32\bin;D:\Espressif\tools\riscv32-esp-elf\esp-13.2.0_20240530\riscv32-esp-elf\bin;D:\Espressif\python_env\idf5.5_py3.11_env\Scripts;$env:PATH"

# Clear MSYSTEM to avoid MSys/Mingw detection
$env:MSYSTEM = $null

Write-Host "========================================"
Write-Host "Building RC Tank Demo - TANK Role"
Write-Host "========================================"

# Set Tank role
$env:SDKCONFIG_DEFAULTS = "sdkconfig.defaults;sdkconfig.defaults.tank"

# Remove old build
if (Test-Path "build") {
    Remove-Item -Recurse -Force "build"
}
if (Test-Path "sdkconfig") {
    Remove-Item -Force "sdkconfig"
}

# Build
Write-Host "Setting target to esp32s3..."
& "D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe" "$env:IDF_PATH\tools\idf.py" set-target esp32s3

Write-Host "Building firmware..."
& "D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe" "$env:IDF_PATH\tools\idf.py" build

if ($LASTEXITCODE -eq 0) {
    Write-Host "========================================"
    Write-Host "Tank firmware built successfully!"
    Write-Host "========================================"

    # Backup firmware
    if (!(Test-Path "firmware_backup")) {
        New-Item -ItemType Directory -Path "firmware_backup" | Out-Null
    }

    Copy-Item "build\rc_tank_demo.bin" "firmware_backup\rc_tank_demo_TANK.bin" -Force
    Copy-Item "build\bootloader\bootloader.bin" "firmware_backup\bootloader_TANK.bin" -Force
    Copy-Item "build\partition_table\partition-table.bin" "firmware_backup\partition-table_TANK.bin" -Force

    Write-Host "Firmware backed up to firmware_backup\"
    Get-ChildItem "firmware_backup\*TANK*.bin" | Format-Table Name, Length
} else {
    Write-Host "========================================"
    Write-Host "Tank firmware build FAILED!"
    Write-Host "========================================"
    exit 1
}
