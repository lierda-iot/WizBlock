# Rebuild Remote firmware with full clean
$ErrorActionPreference = "Stop"

# Already in correct directory, no Set-Location needed

$env:IDF_PATH = "D:\Espressif\frameworks\esp-idf-v5.5.4"
$env:IDF_TOOLS_PATH = "D:\Espressif"
$env:PATH = "D:\Espressif\tools\cmake\3.24.0\bin;D:\Espressif\tools\ninja\1.11.1;D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;D:\Espressif\python_env\idf5.5_py3.11_env\Scripts;" + $env:PATH
$env:SDKCONFIG_DEFAULTS = "sdkconfig.defaults;sdkconfig.defaults.remote"

Write-Host "=== Set target ===" -ForegroundColor Cyan
& D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe $env:IDF_PATH\tools\idf.py set-target esp32s3

Write-Host "=== Build Remote firmware ===" -ForegroundColor Cyan
& D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe $env:IDF_PATH\tools\idf.py build

if ($LASTEXITCODE -eq 0) {
    Write-Host "=== Backup firmware ===" -ForegroundColor Green
    if (-not (Test-Path firmware_backup)) { New-Item -ItemType Directory firmware_backup | Out-Null }
    Copy-Item build\rc_tank_demo.bin firmware_backup\rc_tank_demo_REMOTE.bin -Force
    Copy-Item build\bootloader\bootloader.bin firmware_backup\bootloader_REMOTE.bin -Force
    Copy-Item build\partition_table\partition-table.bin firmware_backup\partition-table_REMOTE.bin -Force
    Write-Host "Remote firmware built and backed up successfully!" -ForegroundColor Green
} else {
    Write-Host "Build FAILED!" -ForegroundColor Red
    exit 1
}
