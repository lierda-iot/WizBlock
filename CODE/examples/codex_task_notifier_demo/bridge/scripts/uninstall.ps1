[CmdletBinding()]
param(
    [string]$InstallDir = "",
    [string]$TaskName = "CodexTaskNotifierBridge"
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
if ($null -ne $task) {
    Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    Unregister-ScheduledTask -TaskName $TaskName -Confirm:$false
}

Write-Host "Bridge scheduled task removed: $TaskName"
Write-Host "State, token, runtime, and logs were retained: $InstallDir"
Write-Host "The Windows Firewall rule was not changed. Remove it separately as Administrator when needed."
