param(
    [Parameter(Mandatory=$true)]
    [string]$Example,
    [switch]$Clean,
    [string]$Port
)

$ErrorActionPreference = "Stop"

Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Remove-Item Env:MSYSTEM_PREFIX -ErrorAction SilentlyContinue
Remove-Item Env:MINGW_PREFIX -ErrorAction SilentlyContinue

$env:PYTHONUTF8 = "1"
$env:IDF_PATH = "D:\Espressif\frameworks\esp-idf-v5.5.4"
$env:IDF_TOOLS_PATH = "D:\Espressif"
$env:IDF_PYTHON_ENV_PATH = "D:\Espressif\python_env\idf5.5_py3.11_env"
$env:ESP_ROM_ELF_DIR = "D:\Espressif\tools\esp-rom-elfs\20241011"
$env:Path = "D:\Espressif\tools\cmake\3.30.2\bin;D:\Espressif\tools\ninja\1.12.1;D:\Espressif\tools\xtensa-esp-elf\esp-14.2.0_20260121\xtensa-esp-elf\bin;" + $env:Path

$python = "D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
$idfPy = "D:\Espressif\frameworks\esp-idf-v5.5.4\tools\idf.py"
$mirrorRoot = "$env:TEMP\laiwfs300_build\CODE"
$exampleDir = Join-Path $mirrorRoot "examples\$Example"

if (-not (Test-Path -LiteralPath $exampleDir)) {
    Write-Error "Example directory not found: $exampleDir (did you run the bash mirror step first?)"
    exit 1
}

if ($Clean) {
    $buildPath = Join-Path $exampleDir "build"
    if (Test-Path -LiteralPath $buildPath) { Remove-Item -LiteralPath $buildPath -Recurse -Force }
    $sdkPath = Join-Path $exampleDir "sdkconfig"
    if (Test-Path -LiteralPath $sdkPath) { Remove-Item -LiteralPath $sdkPath -Force }
}

Push-Location -LiteralPath $exampleDir

if ($Port) {
    & $python $idfPy -p $Port flash
} else {
    & $python $idfPy build
}

Pop-Location
