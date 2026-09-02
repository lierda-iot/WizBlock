#Requires -RunAsAdministrator

[CmdletBinding()]
param(
    [ValidateSet("Add", "Remove")]
    [string]$Action = "Add",
    [ValidateRange(1, 65535)]
    [int]$Port = 8765,
    [string]$InstallDir = ""
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
$pythonExe = Join-Path $InstallDir "venv\Scripts\python.exe"
$ruleName = "Codex Task Notifier Bridge TCP $Port"
$existingRules = @(Get-NetFirewallRule -DisplayName $ruleName `
    -ErrorAction SilentlyContinue)

if ("Remove" -eq $Action) {
    if (0 -lt $existingRules.Count) {
        $existingRules | Remove-NetFirewallRule
    }
    Write-Host "Private LocalSubnet firewall rule removed: $ruleName"
    exit 0
}

if (-not (Test-Path -LiteralPath $pythonExe -PathType Leaf)) {
    throw "Bridge Python was not found. Run install.ps1 first."
}
if (0 -lt $existingRules.Count) {
    $existingRules | Remove-NetFirewallRule
}
New-NetFirewallRule `
    -DisplayName $ruleName `
    -Direction Inbound `
    -Action Allow `
    -Protocol TCP `
    -LocalPort $Port `
    -Program $pythonExe `
    -Profile Private `
    -RemoteAddress LocalSubnet | Out-Null
Write-Host "Private LocalSubnet firewall rule installed: $ruleName"
