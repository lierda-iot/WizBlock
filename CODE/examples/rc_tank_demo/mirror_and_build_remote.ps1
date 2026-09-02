# Mirror CODE to ASCII temp path and build Remote firmware
# Based on design.md 16. 验证入口

$ErrorActionPreference = "Stop"

$IDF_PATH = "D:\Espressif\frameworks\esp-idf-v5.5.4"
$IDF_TOOLS_PATH = "D:\Espressif"
$IDF_PYTHON_ENV_PATH = "D:\Espressif\python_env\idf5.5_py3.11_env"

# Mirror entire CODE to ASCII temp path
$SourceRoot = Get-Location | Split-Path -Parent | Split-Path -Parent
$TempRoot = "C:\Users\15301\AppData\Local\Temp\laiwfs300_build\CODE"
$BuildDir = "$TempRoot\examples\rc_tank_demo"

Write-Host "=== Mirroring CODE to ASCII temp path ===" -ForegroundColor Cyan
Write-Host "Source: $SourceRoot"
Write-Host "Target: $TempRoot"

# Remove old mirror
if (Test-Path $TempRoot) {
    Remove-Item $TempRoot -Recurse -Force
}

# Mirror with robocopy
& robocopy $SourceRoot $TempRoot /E /NFL /NDL /NJH /NJS /NC /NS /NP
$robocopyExit = $LASTEXITCODE
Write-Host "Robocopy exit code: $robocopyExit"
if ($robocopyExit -ge 8) {
    Write-Host "Robocopy failed" -ForegroundColor Red
    exit 1
}

Write-Host "=== Setting up environment ===" -ForegroundColor Cyan
$env:IDF_PATH = $IDF_PATH
$env:IDF_TOOLS_PATH = $IDF_TOOLS_PATH
$env:IDF_PYTHON_ENV_PATH = $IDF_PYTHON_ENV_PATH
$env:PATH = "D:\Espressif\tools\cmake\3.30.2\bin;D:\Espressif\tools\ninja\1.12.1;D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;$IDF_PYTHON_ENV_PATH\Scripts;" + $env:PATH
$env:SDKCONFIG_DEFAULTS = "sdkconfig.defaults;sdkconfig.defaults.remote"

Write-Host "=== Changing to build directory ===" -ForegroundColor Cyan
Set-Location $BuildDir

Write-Host "=== Set target ===" -ForegroundColor Cyan
& "$IDF_PYTHON_ENV_PATH\Scripts\python.exe" "$IDF_PATH\tools\idf.py" set-target esp32s3
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "=== Build Remote firmware ===" -ForegroundColor Cyan
& "$IDF_PYTHON_ENV_PATH\Scripts\python.exe" "$IDF_PATH\tools\idf.py" build
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }

Write-Host "=== Copy firmware back to original location ===" -ForegroundColor Green
$OriginalFirmwareDir = "e:\10__AIProject\7_AI陪伴机器人\CODE\examples\rc_tank_demo\firmware_backup"
if (-not (Test-Path $OriginalFirmwareDir)) {
    New-Item -ItemType Directory $OriginalFirmwareDir | Out-Null
}

Copy-Item "$BuildDir\build\rc_tank_demo.bin" "$OriginalFirmwareDir\rc_tank_demo_REMOTE.bin" -Force
Copy-Item "$BuildDir\build\bootloader\bootloader.bin" "$OriginalFirmwareDir\bootloader_REMOTE.bin" -Force
Copy-Item "$BuildDir\build\partition_table\partition-table.bin" "$OriginalFirmwareDir\partition-table_REMOTE.bin" -Force

Write-Host "=== Remote firmware built successfully! ===" -ForegroundColor Green
Write-Host "Firmware location: $OriginalFirmwareDir\rc_tank_demo_REMOTE.bin"
