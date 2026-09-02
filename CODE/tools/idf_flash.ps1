param(
    [string]$Port = "COM7",
    [string]$MirrorRoot = "$env:TEMP\laiwfs300_build\CODE"
)

$ErrorActionPreference = "Stop"

$idfPath = "D:\Espressif\frameworks\esp-idf-v5.5.4"
$pythonEnv = "D:\Espressif\python_env\idf5.5_py3.11_env"
$python = Join-Path $pythonEnv "Scripts\python.exe"
$idfPy = Join-Path $idfPath "tools\idf.py"

$cmakeBin = "D:\Espressif\tools\cmake\3.30.2\bin"
$ninjaBin = "D:\Espressif\tools\ninja\1.12.1"
$xtensaBin = "D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin"
$romElfDir = "D:\Espressif\tools\esp-rom-elfs\20241011"

foreach ($requiredPath in @($MirrorRoot, (Join-Path $MirrorRoot "build"), $idfPath, $python, $idfPy, $cmakeBin, $ninjaBin, $xtensaBin, $romElfDir)) {
    if (-not (Test-Path -LiteralPath $requiredPath)) {
        throw "Required flash path is missing: $requiredPath. Run tools\idf_build.ps1 first."
    }
}

$env:PYTHONUTF8 = "1"
$env:IDF_PATH = $idfPath
$env:IDF_TOOLS_PATH = "D:\Espressif"
$env:IDF_PYTHON_ENV_PATH = $pythonEnv
$env:ESP_ROM_ELF_DIR = $romElfDir
$env:Path = "$cmakeBin;$ninjaBin;$xtensaBin;$env:Path"

Push-Location -LiteralPath $MirrorRoot
try {
    & $python $idfPy -p $Port flash
    exit $LASTEXITCODE
}
finally {
    Pop-Location
}
