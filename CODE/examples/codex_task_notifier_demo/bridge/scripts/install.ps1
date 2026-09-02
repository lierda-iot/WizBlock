[CmdletBinding()]
param(
    [ValidateRange(1, 65535)]
    [int]$Port = 8765,
    [string]$SessionsDir = "",
    [string]$InstallDir = "",
    [string]$TaskName = "CodexTaskNotifierBridge"
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

function Get-DefaultInstallDir {
    $localAppData = [Environment]::GetEnvironmentVariable("LOCALAPPDATA")
    if ([string]::IsNullOrWhiteSpace($localAppData)) {
        $localAppData = Join-Path $HOME "AppData\Local"
    }
    return Join-Path $localAppData "CodexTaskNotifierDemo"
}

function Find-CompatiblePython {
    $candidates = @(
        [pscustomobject]@{ Command = "py"; PrefixArgs = @("-3.11") },
        [pscustomobject]@{ Command = "py"; PrefixArgs = @("-3") },
        [pscustomobject]@{ Command = "python"; PrefixArgs = @() },
        [pscustomobject]@{ Command = "python3"; PrefixArgs = @() }
    )

    foreach ($candidate in $candidates) {
        $resolved = Get-Command -Name $candidate.Command -CommandType Application `
            -ErrorAction SilentlyContinue | Select-Object -First 1
        if ($null -eq $resolved) {
            continue
        }
        $arguments = @($candidate.PrefixArgs) + @(
            "-c",
            "import sys; raise SystemExit(0 if sys.version_info >= (3, 11) else 1)"
        )
        $null = & $resolved.Path @arguments 2>$null
        if (0 -eq $LASTEXITCODE) {
            return [pscustomobject]@{
                Command = $resolved.Path
                PrefixArgs = @($candidate.PrefixArgs)
            }
        }
    }
    throw "Python 3.11 or newer was not found. Install Python, then rerun this script."
}

function New-BridgeToken {
    $bytes = New-Object byte[] 32
    $generator = [Security.Cryptography.RandomNumberGenerator]::Create()
    try {
        $generator.GetBytes($bytes)
    }
    finally {
        $generator.Dispose()
    }
    return -join ($bytes | ForEach-Object { $_.ToString("x2") })
}

function Write-Utf8NoBom {
    param([string]$Path, [string]$Value)

    $encoding = New-Object System.Text.UTF8Encoding($false)
    [IO.File]::WriteAllText($Path, $Value, $encoding)
}

function Write-Utf8Bom {
    param([string]$Path, [string]$Value)

    $encoding = New-Object System.Text.UTF8Encoding($true)
    [IO.File]::WriteAllText($Path, $Value, $encoding)
}

function Read-BridgeToken {
    param([string]$Path)

    $lines = @(
        Get-Content -LiteralPath $Path -Encoding UTF8 |
            ForEach-Object { $_.Trim() } |
            Where-Object { $_ -and -not $_.StartsWith("#") }
    )
    $prefix = "CODEX_NOTIFIER_TOKEN="
    if (1 -ne $lines.Count -or -not $lines[0].StartsWith($prefix)) {
        throw "The local environment file must contain exactly one CODEX_NOTIFIER_TOKEN entry."
    }
    return $lines[0].Substring($prefix.Length)
}

function ConvertTo-SingleQuotedLiteral {
    param([string]$Value)

    return "'" + $Value.Replace("'", "''") + "'"
}

if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw "This installer supports native Windows only."
}

$scriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
$bridgeDir = [IO.Path]::GetFullPath((Join-Path $scriptDir ".."))
$packageSource = Join-Path $bridgeDir "src\codex_task_bridge"
$envLocal = Join-Path $bridgeDir ".env.local"
if (-not (Test-Path -LiteralPath $packageSource -PathType Container)) {
    throw "Bridge source package was not found."
}

if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Get-DefaultInstallDir
}
$InstallDir = [IO.Path]::GetFullPath($InstallDir)
if ([string]::IsNullOrWhiteSpace($SessionsDir)) {
    $SessionsDir = Join-Path $HOME ".codex\sessions"
}
$SessionsDir = [IO.Path]::GetFullPath($SessionsDir)

$python = Find-CompatiblePython
if (-not (Test-Path -LiteralPath $envLocal -PathType Leaf)) {
    $generatedToken = New-BridgeToken
    Write-Utf8NoBom -Path $envLocal `
        -Value "CODEX_NOTIFIER_TOKEN=$generatedToken`n"
}
$token = Read-BridgeToken -Path $envLocal

$previousPythonPath = $env:PYTHONPATH
$previousToken = $env:CODEX_NOTIFIER_TOKEN
try {
    $env:PYTHONPATH = Join-Path $bridgeDir "src"
    $env:CODEX_NOTIFIER_TOKEN = $token
    $validationArgs = @($python.PrefixArgs) + @(
        "-c",
        'import os; from codex_task_bridge.__main__ import validate_token; validate_token(os.environ["CODEX_NOTIFIER_TOKEN"])'
    )
    $null = & $python.Command @validationArgs
    if (0 -ne $LASTEXITCODE) {
        throw "Token validation failed."
    }
}
finally {
    $env:PYTHONPATH = $previousPythonPath
    $env:CODEX_NOTIFIER_TOKEN = $previousToken
}

$existingTask = Get-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
if ($null -ne $existingTask) {
    Stop-ScheduledTask -TaskName $TaskName -ErrorAction SilentlyContinue
    Start-Sleep -Milliseconds 500
}

New-Item -ItemType Directory -Path $InstallDir -Force | Out-Null
$runtimeDir = Join-Path $InstallDir "runtime"
$runtimeNext = "$runtimeDir.new.$PID"
$runtimeOld = "$runtimeDir.old.$PID"
foreach ($stagingPath in @($runtimeNext, $runtimeOld)) {
    if (Test-Path -LiteralPath $stagingPath) {
        Remove-Item -LiteralPath $stagingPath -Recurse -Force
    }
}
$runtimeSrc = Join-Path $runtimeNext "src"
New-Item -ItemType Directory -Path $runtimeSrc -Force | Out-Null
$runtimePackage = Join-Path $runtimeSrc "codex_task_bridge"
Copy-Item -LiteralPath $packageSource -Destination $runtimePackage -Recurse -Force
if (Test-Path -LiteralPath $runtimeDir) {
    Move-Item -LiteralPath $runtimeDir -Destination $runtimeOld
}
try {
    Move-Item -LiteralPath $runtimeNext -Destination $runtimeDir
}
catch {
    if (Test-Path -LiteralPath $runtimeOld) {
        Move-Item -LiteralPath $runtimeOld -Destination $runtimeDir
    }
    throw
}
if (Test-Path -LiteralPath $runtimeOld) {
    Remove-Item -LiteralPath $runtimeOld -Recurse -Force
}

$venvDir = Join-Path $InstallDir "venv"
$venvPython = Join-Path $venvDir "Scripts\python.exe"
if (Test-Path -LiteralPath $venvPython -PathType Leaf) {
    $null = & $venvPython -c `
        "import sys; raise SystemExit(0 if sys.version_info >= (3, 11) else 1)" `
        2>$null
    if (0 -ne $LASTEXITCODE) {
        Remove-Item -LiteralPath $venvDir -Recurse -Force
    }
}
if (-not (Test-Path -LiteralPath $venvPython -PathType Leaf)) {
    $venvArgs = @($python.PrefixArgs) + @("-m", "venv", $venvDir)
    $null = & $python.Command @venvArgs
    if (0 -ne $LASTEXITCODE -or
        -not (Test-Path -LiteralPath $venvPython -PathType Leaf)) {
        throw "Failed to create the Bridge virtual environment."
    }
}

$runtimeEnv = Join-Path $InstallDir ".env"
[IO.File]::Copy($envLocal, $runtimeEnv, $true)
$stateFile = Join-Path $InstallDir "bridge-state.json"
$runnerPath = Join-Path $InstallDir "run-bridge.ps1"
$logPath = Join-Path $InstallDir "bridge.log"
$errorLogPath = Join-Path $InstallDir "bridge.err.log"
$runnerTemplate = @'
Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"
$envFile = __ENV_FILE__
$errorLog = __ERROR_LOG__
try {
    $tokenLine = Get-Content -LiteralPath $envFile -Encoding UTF8 |
        Where-Object { $_ -match '^CODEX_NOTIFIER_TOKEN=.+' } |
        Select-Object -First 1
    if ($null -eq $tokenLine) {
        throw "Missing Bridge token."
    }
    $env:CODEX_NOTIFIER_TOKEN = $tokenLine.Substring("CODEX_NOTIFIER_TOKEN=".Length)
    $env:PYTHONPATH = __PYTHON_PATH__
    & __PYTHON_EXE__ -m codex_task_bridge `
        --host 0.0.0.0 `
        --port __PORT__ `
        --sessions-dir __SESSIONS_DIR__ `
        --state-file __STATE_FILE__ `
        1>> __LOG_PATH__ 2>> $errorLog
    exit $LASTEXITCODE
}
catch {
    "[runner] failed type=$($_.Exception.GetType().Name)" |
        Add-Content -LiteralPath $errorLog -Encoding UTF8
    exit 1
}
'@
$runner = $runnerTemplate.Replace(
    "__ENV_FILE__", (ConvertTo-SingleQuotedLiteral $runtimeEnv)
).Replace(
    "__ERROR_LOG__", (ConvertTo-SingleQuotedLiteral $errorLogPath)
).Replace(
    "__PYTHON_PATH__", (ConvertTo-SingleQuotedLiteral (Join-Path $runtimeDir "src"))
).Replace(
    "__PYTHON_EXE__", (ConvertTo-SingleQuotedLiteral $venvPython)
).Replace(
    "__PORT__", $Port.ToString([Globalization.CultureInfo]::InvariantCulture)
).Replace(
    "__SESSIONS_DIR__", (ConvertTo-SingleQuotedLiteral $SessionsDir)
).Replace(
    "__STATE_FILE__", (ConvertTo-SingleQuotedLiteral $stateFile)
).Replace(
    "__LOG_PATH__", (ConvertTo-SingleQuotedLiteral $logPath)
)
Write-Utf8Bom -Path $runnerPath -Value $runner

$currentUser = [Security.Principal.WindowsIdentity]::GetCurrent().Name
$powerShellExe = (Get-Process -Id $PID).Path
$actionArguments = "-NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass " +
    "-WindowStyle Hidden -File `"$runnerPath`""
$action = New-ScheduledTaskAction -Execute $powerShellExe `
    -Argument $actionArguments -WorkingDirectory $InstallDir
$trigger = New-ScheduledTaskTrigger -AtLogOn -User $currentUser
$principal = New-ScheduledTaskPrincipal -UserId $currentUser `
    -LogonType Interactive -RunLevel Limited
$settings = New-ScheduledTaskSettingsSet `
    -RestartCount 999 `
    -RestartInterval (New-TimeSpan -Minutes 1) `
    -ExecutionTimeLimit ([TimeSpan]::Zero) `
    -MultipleInstances IgnoreNew `
    -StartWhenAvailable `
    -AllowStartIfOnBatteries `
    -DontStopIfGoingOnBatteries
$task = New-ScheduledTask -Action $action -Trigger $trigger `
    -Principal $principal -Settings $settings `
    -Description "Read-only Codex task status bridge for ESP32."
Register-ScheduledTask -TaskName $TaskName -InputObject $task -Force | Out-Null
Start-ScheduledTask -TaskName $TaskName

$apiReady = $false
$headers = @{ "X-Codex-Notifier-Token" = $token }
for ($attempt = 0; $attempt -lt 20; $attempt++) {
    Start-Sleep -Milliseconds 500
    try {
        $null = Invoke-RestMethod `
            -Uri "http://127.0.0.1:$Port/api/v1/state?after_event_seq=0" `
            -Headers $headers -TimeoutSec 1
        $apiReady = $true
        break
    }
    catch {
        continue
    }
}

Write-Host "Bridge scheduled task installed: $TaskName"
Write-Host "Runtime data: $InstallDir"
Write-Host "Sessions directory: $SessionsDir"
Write-Host "Local token file: $envLocal"
if ($apiReady) {
    Write-Host "Local authenticated API check: OK"
}
else {
    Write-Warning "The API was not ready within 10 seconds. Run status.ps1 for diagnostics."
}
Write-Host "Windows Firewall is unchanged. Run configure-firewall.ps1 as Administrator before ESP32 access."
