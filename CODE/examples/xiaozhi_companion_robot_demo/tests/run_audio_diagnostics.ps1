param(
    [string]$Port = "COM7",
    [ValidateRange(10, 3600)]
    [int]$DurationSec = 180,
    [ValidateRange(0, 20)]
    [int]$ExpectedPlaybackWakes = 5,
    [string]$OutputDirectory = "",
    [switch]$Strict
)

$ErrorActionPreference = "Stop"

$testRoot = $PSScriptRoot
$repoRoot = (Resolve-Path (Join-Path $testRoot "..\..\..\..")).Path
if ([string]::IsNullOrWhiteSpace($OutputDirectory)) {
    $OutputDirectory = Join-Path $repoRoot "robotlog"
}
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null

$stamp = Get-Date -Format "yyyy-MM-dd_HH-mm-ss"
$logPath = Join-Path $OutputDirectory ("{0}_ex024-audio-auto-{1}.txt" -f $stamp, $Port.ToLowerInvariant())
$summaryPath = [IO.Path]::ChangeExtension($logPath, ".audio-diagnostics.md")

$serial = [System.IO.Ports.SerialPort]::new(
    $Port,
    115200,
    [System.IO.Ports.Parity]::None,
    8,
    [System.IO.Ports.StopBits]::One
)
$serial.ReadTimeout = 100
$serial.DtrEnable = $false
$serial.RtsEnable = $false
$writer = $null

try {
    $serial.Open()
    $writer = [IO.StreamWriter]::new(
        $logPath,
        $false,
        [Text.UTF8Encoding]::new($false)
    )
    $writer.AutoFlush = $true
    $writer.WriteLine(
        "[AUTO_CAPTURE] port={0} baud=115200 duration_sec={1} dtr=0 rts=0 start={2:o}" -f
        $Port,
        $DurationSec,
        (Get-Date)
    )
    Write-Host "Capturing $Port for $DurationSec seconds -> $logPath"

    $deadline = (Get-Date).AddSeconds($DurationSec)
    while ((Get-Date) -lt $deadline) {
        $chunk = $serial.ReadExisting()
        if (-not [string]::IsNullOrEmpty($chunk)) {
            $writer.Write($chunk)
        }
        Start-Sleep -Milliseconds 50
    }
    $writer.WriteLine("")
    $writer.WriteLine("[AUTO_CAPTURE] end={0:o}" -f (Get-Date))
}
finally {
    if ($null -ne $writer) {
        $writer.Flush()
        $writer.Dispose()
    }
    if ($serial.IsOpen) {
        $serial.Close()
    }
    $serial.Dispose()
}

$pythonCommand = Get-Command python, python3 -ErrorAction SilentlyContinue |
    Select-Object -First 1
$pythonPath = $null
if ($null -ne $pythonCommand) {
    $pythonPath = $pythonCommand.Source
}
if ([string]::IsNullOrWhiteSpace($pythonPath)) {
    $documentedPython = "D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
    if (Test-Path -LiteralPath $documentedPython) {
        $pythonPath = $documentedPython
    }
}
if ([string]::IsNullOrWhiteSpace($pythonPath)) {
    throw "Python was not found; raw capture is saved at $logPath."
}

$analyzer = Join-Path $testRoot "analyze_audio_diagnostics.py"
$analyzerArgs = @(
    $analyzer,
    $logPath,
    "--output",
    $summaryPath,
    "--expected-playback-wakes",
    $ExpectedPlaybackWakes
)
if ($Strict) {
    $analyzerArgs += "--strict"
}
& $pythonPath @analyzerArgs
if (0 -ne $LASTEXITCODE) {
    Write-Warning "Diagnostic gates reported a failure. Raw capture remains at $logPath."
    exit $LASTEXITCODE
}

$hash = Get-FileHash -Algorithm SHA256 -LiteralPath $logPath
Write-Host ("RAW_LOG={0}" -f $logPath)
Write-Host ("SUMMARY={0}" -f $summaryPath)
Write-Host ("SHA256={0}" -f $hash.Hash)
