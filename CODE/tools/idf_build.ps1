param(
    [string]$MirrorRoot = "$env:TEMP\laiwfs300_build\CODE",
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

$codeRoot = (Resolve-Path -LiteralPath (Join-Path $PSScriptRoot "..")).Path
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

$mirrorParent = Split-Path -Parent $mirrorRoot
New-Item -ItemType Directory -Force -Path $mirrorParent | Out-Null

$robocopyArgs = @(
    $codeRoot,
    $mirrorRoot,
    "/MIR",
    "/XD", "build",
    "/XF", "sdkconfig"
)
& robocopy @robocopyArgs | Out-Null
if ($LASTEXITCODE -gt 7) {
    throw "robocopy failed with exit code $LASTEXITCODE"
}

if (-not (Test-Path -LiteralPath (Join-Path $mirrorRoot "CMakeLists.txt"))) {
    throw "Build mirror is invalid: $mirrorRoot"
}

$env:PYTHONUTF8 = "1"
$env:IDF_PATH = $idfPath
$env:IDF_TOOLS_PATH = "D:\Espressif"
$env:IDF_PYTHON_ENV_PATH = $pythonEnv
$env:ESP_ROM_ELF_DIR = $romElfDir
$env:Path = "$cmakeBin;$ninjaBin;$xtensaBin;$env:Path"

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
    & $python $idfPy build
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
