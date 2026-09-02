param(
    [string]$Port = "COM7",
    [string]$Role = "TANK"
)

$ErrorActionPreference = "Stop"

$projectRoot = $PSScriptRoot
$firmwareBackup = Join-Path $projectRoot "firmware_backup"

$pythonEnv = "D:\Espressif\python_env\idf5.5_py3.11_env"
$python = Join-Path $pythonEnv "Scripts\python.exe"

if (-not (Test-Path $python)) {
    throw "Python not found: $python"
}

$bootloader = Join-Path $firmwareBackup "bootloader_$Role.bin"
$partitionTable = Join-Path $firmwareBackup "partition-table_$Role.bin"
$app = Join-Path $firmwareBackup "rc_tank_demo_$Role.bin"

foreach ($file in @($bootloader, $partitionTable, $app)) {
    if (-not (Test-Path $file)) {
        throw "Firmware file not found: $file"
    }
}

Write-Host "========================================="
Write-Host "Flashing RC Tank Demo - $Role Role to $Port"
Write-Host "========================================="
Write-Host "Bootloader:      $bootloader"
Write-Host "Partition Table: $partitionTable"
Write-Host "Application:     $app"
Write-Host ""

# Erase flash first
Write-Host "Erasing flash..."
& $python -m esptool --chip esp32s3 --port $Port erase_flash
if ($LASTEXITCODE -ne 0) {
    throw "Flash erase failed"
}

Write-Host ""
Write-Host "Programming flash..."
& $python -m esptool --chip esp32s3 --port $Port --baud 460800 `
    --before default_reset --after hard_reset write_flash `
    --flash_mode dio --flash_size 8MB --flash_freq 80m `
    0x0 $bootloader `
    0x8000 $partitionTable `
    0x10000 $app

if ($LASTEXITCODE -eq 0) {
    Write-Host ""
    Write-Host "========================================="
    Write-Host "Flash completed successfully!"
    Write-Host "========================================="
} else {
    throw "Flash programming failed"
}
