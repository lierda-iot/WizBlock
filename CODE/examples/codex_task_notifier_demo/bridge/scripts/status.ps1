[CmdletBinding()]
param(
    [ValidateRange(1, 65535)]
    [int]$Port = 8765,
    [string]$InstallDir = "",
    [string]$TaskName = "CodexTaskNotifierBridge",
    [ValidateRange(0, 200)]
    [int]$Tail = 20
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $localAppData = [Environment]::GetEnvironmentVariable("LOCALAPPDATA")
    if ([string]::IsNullOrWhiteSpace($localAppData)) {
        $localAppData = Join-Path $HOME "AppData\Local"
    }
    $InstallDir = Join-Path $localAppData "CodexTaskNotifierDemo"
}
$InstallDir = [IO.Path]::GetFullPath($InstallDir)

$task = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($null -eq $task) {
    Write-Host "Scheduled task: NOT INSTALLED"
    exit 1
}
$taskInfo = Get-ScheduledTaskInfo -TaskName $TaskName
Write-Host "Scheduled task: $($task.State)"
Write-Host "Last task result: $($taskInfo.LastTaskResult)"
Write-Host "Last run time: $($taskInfo.LastRunTime)"

$envFile = Join-Path $InstallDir ".env"
$tokenLine = $null
if (Test-Path -LiteralPath $envFile -PathType Leaf) {
    $tokenLine = Get-Content -LiteralPath $envFile -Encoding UTF8 |
        Where-Object { $_ -match '^CODEX_NOTIFIER_TOKEN=.+' } |
        Select-Object -First 1
}
if ($null -eq $tokenLine) {
    Write-Host "Local authenticated API: CONFIGURATION MISSING"
}
else {
    $token = $tokenLine.Substring("CODEX_NOTIFIER_TOKEN=".Length)
    try {
        $response = Invoke-RestMethod `
            -Uri "http://127.0.0.1:$Port/api/v1/state?after_event_seq=0" `
            -Headers @{ "X-Codex-Notifier-Token" = $token } `
            -TimeoutSec 3
        Write-Host "Local authenticated API: OK"
        Write-Host "Visible tasks: $($response.tasks.Count)"
    }
    catch {
        Write-Host "Local authenticated API: UNREACHABLE"
    }
}

$errorLog = Join-Path $InstallDir "bridge.err.log"
if ((0 -lt $Tail) -and
    (Test-Path -LiteralPath $errorLog -PathType Leaf)) {
    Write-Host "Bridge log tail:"
    Get-Content -LiteralPath $errorLog -Tail $Tail
}
