param(
    [string]$Port = 'COM7',
    [int]$Duration = 90,
    [string]$Tag = 'round8'
)
$ErrorActionPreference = 'Continue'
$timestamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$logFile = Join-Path (Get-Location) "${Tag}_${timestamp}.txt"
$python = 'D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe'

Write-Host "=== capturing $Duration s from $Port -> $logFile ==="

$job = Start-Job -ScriptBlock {
    param($py, $p, $d)
    & $py -m serial.tools.miniterm --raw $p 115200 --exit-char 0x03 |
        Select-Object -First ($d * 25)
} -ArgumentList $python, $Port, $Duration

Start-Sleep -Seconds $Duration
Stop-Job $job -ErrorAction SilentlyContinue
$output = Receive-Job $job
Remove-Job $job -Force

$output | Out-File -FilePath $logFile -Encoding UTF8
Write-Host "saved: $logFile"
Write-Host "lines: $($output.Count)"
Write-Host ''
Write-Host '=== metrics ==='
$usbErr = ($output | Select-String -Pattern 'Failed to register|usb_host.*fail|install.*failed').Count
$imei   = ($output | Select-String -Pattern 'Module IMEI').Count
$gotIP  = ($output | Select-String -Pattern 'Got IP:').Count
$conn   = ($output | Select-String -Pattern '4G Device Connected').Count
$badHdr = ($output | Select-String -Pattern 'invalid header').Count
Write-Host "USB_ERR=$usbErr"
Write-Host "IMEI=$imei"
Write-Host "GOT_IP=$gotIP"
Write-Host "4G_CONNECTED=$conn"
Write-Host "BAD_HEADER=$badHdr"
