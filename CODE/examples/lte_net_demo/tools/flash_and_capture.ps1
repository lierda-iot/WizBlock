param(
    [string]$Port = "COM7",
    [int]$CaptureSeconds = 100,
    [string]$Tag = "round",
    [switch]$SkipFlash
)

$ErrorActionPreference = "Stop"

# --- Wait for the serial port to enumerate ---
Write-Host "[1/4] Waiting for port $Port ..."
$found = $false
for ($i = 0; $i -lt 15; $i++) {
    $ports = [System.IO.Ports.SerialPort]::GetPortNames()
    if ($ports -contains $Port) { $found = $true; break }
    Write-Host ("  attempt {0}: ports=[{1}]" -f $i, ($ports -join ","))
    Start-Sleep -Seconds 2
}
if (-not $found) {
    Write-Error "Port $Port not found after waiting. Available: $([System.IO.Ports.SerialPort]::GetPortNames() -join ',')"
    exit 2
}
Write-Host "  $Port available."

# --- Flash via idf.py ---
if ($SkipFlash) {
    Write-Host "[2/4] SkipFlash set: reusing firmware already on device."
} else {
Write-Host "[2/4] Flashing firmware to $Port ..."
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
$exampleDir = "$env:TEMP\laiwfs300_build\CODE\examples\lte_net_demo"

Push-Location -LiteralPath $exampleDir
& $python $idfPy -p $Port flash
$flashRc = $LASTEXITCODE
Pop-Location
if ($flashRc -ne 0) { Write-Error "Flash failed rc=$flashRc"; exit 3 }
Write-Host "  Flash OK."
}

# --- Capture serial log ---
# Derive log dir from script location at runtime to avoid hardcoding a path
# with non-ASCII chars (PowerShell 5.x mis-decodes UTF-8 script bytes as GBK).
$logDir = Join-Path (Split-Path -Parent $PSScriptRoot) "log"
if (-not (Test-Path $logDir)) { New-Item -ItemType Directory -Path $logDir | Out-Null }
$stamp = Get-Date -Format "yyyyMMdd_HHmmss"
$logFile = Join-Path $logDir ("{0}_{1}.txt" -f $Tag, $stamp)
Write-Host "[3/4] Capturing $CaptureSeconds s from $Port -> $logFile"

# Re-wait for port (device resets after flash)
Start-Sleep -Seconds 2
for ($i = 0; $i -lt 15; $i++) {
    if ([System.IO.Ports.SerialPort]::GetPortNames() -contains $Port) { break }
    Start-Sleep -Seconds 1
}

$sp = New-Object System.IO.Ports.SerialPort($Port, 115200, "None", 8, "One")
$sp.ReadTimeout = 1000
$sp.Open()
# Reset the ESP32-S3 via RTS (EN pin) so capture starts from a clean boot.
# After idf.py flash the device already hard-reset; on -SkipFlash we must reset
# it here to catch the WiFi/4G startup sequence.
if ($SkipFlash) {
    $sp.DtrEnable = $false
    $sp.RtsEnable = $true
    Start-Sleep -Milliseconds 100
    $sp.RtsEnable = $false
    Start-Sleep -Milliseconds 100
    $sp.DiscardInBuffer()
}
$deadline = (Get-Date).AddSeconds($CaptureSeconds)
$sw = [System.IO.StreamWriter]::new($logFile, $false, [System.Text.Encoding]::UTF8)
try {
    while ((Get-Date) -lt $deadline) {
        try {
            $line = $sp.ReadLine()
            $sw.WriteLine($line)
            $sw.Flush()
        } catch [System.TimeoutException] { }
    }
} finally {
    $sw.Close()
    $sp.Close()
}
Write-Host "[4/4] Capture done -> $logFile"
Write-Output "LOGFILE=$logFile"
