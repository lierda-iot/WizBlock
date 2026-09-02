param(
    [string]$Role = "tank",  # "tank" or "remote"
    [string]$MirrorRoot = "$env:TEMP\laiwfs300_build\rc_tank_demo",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$projectRoot = $PSScriptRoot
$mirrorRoot = $MirrorRoot

$idfPath = "D:\Espressif\frameworks\esp-idf-v5.5.4"
$pythonEnv = "D:\Espressif\python_env\idf5.5_py3.11_env"
$python = Join-Path $pythonEnv "Scripts\python.exe"
$idfPy = Join-Path $idfPath "tools\idf.py"

$cmakeBin = "D:\Espressif\tools\cmake\3.30.2\bin"
$ninjaBin = "D:\Espressif\tools\ninja\1.12.1"
$xtensaBin = "D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin"
$romElfDir = "D:\Espressif\tools\esp-rom-elfs\20241011"

foreach ($requiredPath in @($idfPath, $python, $idfPy, $cmakeBin, $ninjaBin, $xtensaBin, $romElfDir)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required ESP-IDF path is missing: $requiredPath"
    }
}

# Mirror project and components
$mirrorParent = Split-Path -Parent $mirrorRoot
New-Item -ItemType Directory -Force -Path $mirrorParent | Out-Null

# Copy project files
$robocopyArgs = @(
    $projectRoot,
    $mirrorRoot,
    "/MIR",
    "/XD", "build", "firmware_backup",
    "/XF", "sdkconfig", "*.bat", "*.md"
)
& robocopy @robocopyArgs | Out-Null
if ($LASTEXITCODE -gt 7) {
    throw "robocopy project failed with exit code $LASTEXITCODE"
}

# Copy components directory
$componentsSource = Join-Path (Split-Path (Split-Path $projectRoot)) "components"
$componentsMirror = Join-Path $mirrorRoot "components"
$robocopyComponentsArgs = @(
    $componentsSource,
    $componentsMirror,
    "/MIR",
    "/XD", "build",
    "/XF", "sdkconfig"
)
& robocopy @robocopyComponentsArgs | Out-Null
if ($LASTEXITCODE -gt 7) {
    throw "robocopy components failed with exit code $LASTEXITCODE"
}

# Fix CMakeLists.txt to use local components
$cmakeListsPath = Join-Path $mirrorRoot "CMakeLists.txt"
$cmakeContent = Get-Content $cmakeListsPath -Raw
$cmakeContent = $cmakeContent -replace 'set\(EXTRA_COMPONENT_DIRS "../../components"\)', 'set(EXTRA_COMPONENT_DIRS "components")'
Set-Content -Path $cmakeListsPath -Value $cmakeContent -NoNewline

if (-not (Test-Path -LiteralPath $cmakeListsPath)) {
    throw "Build mirror is invalid: $mirrorRoot"
}

$env:PYTHONUTF8 = "1"
$env:IDF_PATH = $idfPath
$env:IDF_TOOLS_PATH = "D:\Espressif"
$env:IDF_PYTHON_ENV_PATH = $pythonEnv
$env:ESP_ROM_ELF_DIR = $romElfDir
$env:Path = "$cmakeBin;$ninjaBin;$xtensaBin;$env:Path"

# Set role-specific sdkconfig
if ($Role -eq "tank") {
    $env:SDKCONFIG_DEFAULTS = "sdkconfig.defaults;sdkconfig.defaults.tank"
    Write-Host "Building RC Tank Demo - TANK Role"
} elseif ($Role -eq "remote") {
    $env:SDKCONFIG_DEFAULTS = "sdkconfig.defaults;sdkconfig.defaults.remote"
    Write-Host "Building RC Tank Demo - REMOTE Role"
} else {
    throw "Invalid role: $Role. Must be 'tank' or 'remote'"
}

if ($Clean) {
    $buildPath = Join-Path $mirrorRoot "build"
    if (Test-Path -LiteralPath $buildPath) {
        Remove-Item -LiteralPath $buildPath -Recurse -Force
    }
}

foreach ($configFile in @("sdkconfig", "sdkconfig.old")) {
    $configPath = Join-Path $mirrorRoot $configFile
    if (Test-Path -LiteralPath $configPath) {
        Remove-Item -LiteralPath $configPath -Force
    }
}

Push-Location -LiteralPath $mirrorRoot
try {
    Write-Host "Setting target to esp32s3..."
    & $python $idfPy -D CMAKE_DISABLE_FIND_PACKAGE_Git=TRUE set-target esp32s3
    if ($LASTEXITCODE -ne 0) {
        throw "set-target failed"
    }

    Write-Host "Building firmware..."
    & $python $idfPy -D CMAKE_DISABLE_FIND_PACKAGE_Git=TRUE build
    if ($LASTEXITCODE -ne 0) {
        throw "build failed"
    }

    # Copy binaries back to original project
    $firmwareBackup = Join-Path $projectRoot "firmware_backup"
    New-Item -ItemType Directory -Force -Path $firmwareBackup | Out-Null

    $roleUpper = $Role.ToUpper()
    Copy-Item (Join-Path $mirrorRoot "build\rc_tank_demo.bin") (Join-Path $firmwareBackup "rc_tank_demo_$roleUpper.bin") -Force
    Copy-Item (Join-Path $mirrorRoot "build\bootloader\bootloader.bin") (Join-Path $firmwareBackup "bootloader_$roleUpper.bin") -Force
    Copy-Item (Join-Path $mirrorRoot "build\partition_table\partition-table.bin") (Join-Path $firmwareBackup "partition-table_$roleUpper.bin") -Force

    Write-Host "Firmware backed up to $firmwareBackup"
    Get-ChildItem (Join-Path $firmwareBackup "*$roleUpper*.bin") | Format-Table Name, Length

    exit 0
}
finally {
    Pop-Location
}
