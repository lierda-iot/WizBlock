param(
    [string]$ClangPath
)

$ErrorActionPreference = 'Stop'

if (-not $ClangPath) {
    $clangCommand = Get-Command clang.exe -ErrorAction SilentlyContinue
    if ($clangCommand) {
        $ClangPath = $clangCommand.Source
    }
}

if (-not $ClangPath) {
    $registryKeys = @(
        'HKLM:\SOFTWARE\LLVM\LLVM',
        'HKLM:\SOFTWARE\WOW6432Node\LLVM\LLVM',
        'HKCU:\SOFTWARE\LLVM\LLVM'
    )
    foreach ($registryKey in $registryKeys) {
        if (Test-Path -LiteralPath $registryKey) {
            $llvmRoot = (Get-Item -LiteralPath $registryKey).GetValue('')
            $candidate = Join-Path $llvmRoot 'bin\clang.exe'
            if (Test-Path -LiteralPath $candidate) {
                $ClangPath = $candidate
                break
            }
        }
    }
}

if (-not $ClangPath -or -not (Test-Path -LiteralPath $ClangPath)) {
    throw 'A host clang.exe was not found. Pass its absolute path with -ClangPath.'
}

$componentDirectory = Split-Path -Parent $PSScriptRoot
$buildDirectory = Join-Path $PSScriptRoot 'build'
New-Item -ItemType Directory -Force -Path $buildDirectory | Out-Null

$targetTriple = & $ClangPath -print-target-triple
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$commonArguments = @(
    '-std=c11',
    '-Wall',
    '-Wextra',
    '-Werror',
    '-ffreestanding',
    '-fno-builtin',
    '-fuse-ld=lld',
    '-nostdlib',
    '-Wl,/entry:main',
    '-Wl,/subsystem:console',
    "-I$(Join-Path $PSScriptRoot 'support')",
    "-I$(Join-Path $componentDirectory 'include')",
    "-I$(Join-Path $componentDirectory 'private_include')"
)

function Invoke-HostTest {
    param(
        [string]$Name,
        [string[]]$Sources,
        [string[]]$ExtraArguments = @()
    )

    $executable = Join-Path $buildDirectory "$Name.exe"
    $arguments = $commonArguments + $ExtraArguments + $Sources + @('-o', $executable)
    & $ClangPath @arguments
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }

    & $executable
    $testExit = $LASTEXITCODE
    Write-Output "$Name exit=$testExit"
    if ($testExit -ne 0) {
        exit $testExit
    }
}

$freestandingSupport = Join-Path $PSScriptRoot 'support\freestanding.c'
Invoke-HostTest -Name 'test_network_manager_tuning' -ExtraArguments @(
    '-DCONFIG_NETWORK_MANAGER_INITIAL_ATTEMPT_TIMEOUT_MS=17',
    '-DCONFIG_NETWORK_MANAGER_WIFI_CONNECT_STABLE_MS=19',
    '-DCONFIG_NETWORK_MANAGER_WIFI_DISCONNECT_STABLE_MS=23',
    '-DCONFIG_NETWORK_MANAGER_WIFI_RETRY_LIMIT=3',
    '-DCONFIG_NETWORK_MANAGER_WIFI_RETRY_UNLIMITED=1',
    '-DCONFIG_NETWORK_MANAGER_WIFI_RETRY_INITIAL_BACKOFF_MS=29',
    '-DCONFIG_NETWORK_MANAGER_WIFI_RETRY_MAX_BACKOFF_MS=31',
    '-DCONFIG_NETWORK_MANAGER_CELLULAR_INITIAL_IPV4_WAIT_MS=37',
    '-DCONFIG_NETWORK_MANAGER_CELLULAR_POWER_OFF_HOLD_MS=43'
) -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_network_manager_tuning.c')
)
Invoke-HostTest -Name 'test_network_manager_tuning_idf_config' -ExtraArguments @(
    '-DESP_PLATFORM',
    "-I$(Join-Path $PSScriptRoot 'support\idf_config')"
) -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_network_manager_tuning_idf_config.c')
)
Invoke-HostTest -Name 'test_network_manager_wifi_config' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_network_manager_wifi_config.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_config.c')
)
Invoke-HostTest -Name 'test_network_manager_policy' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_network_manager_policy.c'),
    (Join-Path $componentDirectory 'network_manager_policy.c')
)
Invoke-HostTest -Name 'test_network_manager_event_journal' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_network_manager_event_journal.c'),
    (Join-Path $componentDirectory 'network_manager_event_journal.c')
)
Invoke-HostTest -Name 'test_network_manager_fault_history' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_network_manager_fault_history.c'),
    (Join-Path $componentDirectory 'network_manager_event_journal.c')
)
Invoke-HostTest -Name 'test_network_manager_storage' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_network_manager_storage.c'),
    (Join-Path $componentDirectory 'network_manager_storage_model.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_config.c')
)
Invoke-HostTest -Name 'test_network_manager_storage_nvs' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_network_manager_storage_nvs.c'),
    (Join-Path $componentDirectory 'network_manager_storage_nvs.c'),
    (Join-Path $componentDirectory 'network_manager_storage_model.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_config.c')
)
Invoke-HostTest -Name 'test_network_manager_wifi_runtime_model' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_network_manager_wifi_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_runtime_model.c')
)
Invoke-HostTest -Name 'test_network_manager_wifi_retry_unlimited' -ExtraArguments @(
    '-DCONFIG_NETWORK_MANAGER_WIFI_RETRY_UNLIMITED=1',
    '-DCONFIG_NETWORK_MANAGER_WIFI_RETRY_LIMIT=255',
    '-DCONFIG_NETWORK_MANAGER_WIFI_RETRY_INITIAL_BACKOFF_MS=250',
    '-DCONFIG_NETWORK_MANAGER_WIFI_RETRY_MAX_BACKOFF_MS=500'
) -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_network_manager_wifi_retry_unlimited.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_runtime_model.c')
)
Invoke-HostTest -Name 'test_network_manager_cellular_runtime_model' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_network_manager_cellular_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_cellular_runtime_model.c')
)
Invoke-HostTest -Name 'test_network_manager_dual_runtime_model' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_network_manager_dual_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_dual_runtime_model.c')
)
Invoke-HostTest -Name 'test_network_manager_facade_host' -ExtraArguments @(
    "-I$(Join-Path $PSScriptRoot 'support\fake')",
    '-DNETWORK_MANAGER_HOST_TEST',
    '-DCONFIG_NETWORK_MANAGER_CELLULAR_POWER_OFF_HOLD_MS=37'
) -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'support\fake_host.c'),
    (Join-Path $PSScriptRoot 'test_network_manager_facade_host.c'),
    (Join-Path $componentDirectory 'network_manager.c'),
    (Join-Path $componentDirectory 'network_manager_policy.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_config.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_scan.c'),
    (Join-Path $componentDirectory 'network_manager_event_journal.c'),
    (Join-Path $componentDirectory 'network_manager_storage_model.c'),
    (Join-Path $componentDirectory 'network_manager_storage_nvs.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_cellular_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_dual_runtime_model.c')
)
Invoke-HostTest -Name 'test_network_manager_facade_wifi_host' -ExtraArguments @(
    "-I$(Join-Path $PSScriptRoot 'support\fake')",
    '-DNETWORK_MANAGER_HOST_TEST'
) -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'support\fake_host.c'),
    (Join-Path $PSScriptRoot 'test_network_manager_facade_wifi_host.c'),
    (Join-Path $componentDirectory 'network_manager.c'),
    (Join-Path $componentDirectory 'network_manager_policy.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_config.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_scan.c'),
    (Join-Path $componentDirectory 'network_manager_event_journal.c'),
    (Join-Path $componentDirectory 'network_manager_storage_model.c'),
    (Join-Path $componentDirectory 'network_manager_storage_nvs.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_cellular_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_dual_runtime_model.c')
)
Invoke-HostTest -Name 'test_network_manager_wifi_scan_logic' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_network_manager_wifi_scan.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_scan.c')
)
Invoke-HostTest -Name 'test_network_manager_facade_wifi_scan_host' -ExtraArguments @(
    "-I$(Join-Path $PSScriptRoot 'support\fake')",
    '-DNETWORK_MANAGER_HOST_TEST'
) -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'support\fake_host.c'),
    (Join-Path $PSScriptRoot 'test_network_manager_facade_wifi_scan_host.c'),
    (Join-Path $componentDirectory 'network_manager.c'),
    (Join-Path $componentDirectory 'network_manager_policy.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_config.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_scan.c'),
    (Join-Path $componentDirectory 'network_manager_event_journal.c'),
    (Join-Path $componentDirectory 'network_manager_storage_model.c'),
    (Join-Path $componentDirectory 'network_manager_storage_nvs.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_cellular_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_dual_runtime_model.c')
)
Invoke-HostTest -Name 'test_network_manager_facade_dual_failure_host' -ExtraArguments @(
    "-I$(Join-Path $PSScriptRoot 'support\fake')",
    '-DNETWORK_MANAGER_HOST_TEST'
) -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'support\fake_host.c'),
    (Join-Path $PSScriptRoot 'test_network_manager_facade_dual_failure_host.c'),
    (Join-Path $componentDirectory 'network_manager.c'),
    (Join-Path $componentDirectory 'network_manager_policy.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_config.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_scan.c'),
    (Join-Path $componentDirectory 'network_manager_event_journal.c'),
    (Join-Path $componentDirectory 'network_manager_storage_model.c'),
    (Join-Path $componentDirectory 'network_manager_storage_nvs.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_cellular_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_dual_runtime_model.c')
)
Invoke-HostTest -Name 'test_network_manager_facade_retry_host' -ExtraArguments @(
    "-I$(Join-Path $PSScriptRoot 'support\fake')",
    '-DNETWORK_MANAGER_HOST_TEST'
) -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'support\fake_host.c'),
    (Join-Path $PSScriptRoot 'test_network_manager_facade_retry_host.c'),
    (Join-Path $componentDirectory 'network_manager.c'),
    (Join-Path $componentDirectory 'network_manager_policy.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_config.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_scan.c'),
    (Join-Path $componentDirectory 'network_manager_event_journal.c'),
    (Join-Path $componentDirectory 'network_manager_storage_model.c'),
    (Join-Path $componentDirectory 'network_manager_storage_nvs.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_cellular_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_dual_runtime_model.c')
)
Invoke-HostTest -Name 'test_network_manager_facade_reconnect_terminal_host' -ExtraArguments @(
    "-I$(Join-Path $PSScriptRoot 'support\fake')",
    '-DNETWORK_MANAGER_HOST_TEST'
) -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'support\fake_host.c'),
    (Join-Path $PSScriptRoot 'test_network_manager_facade_reconnect_terminal_host.c'),
    (Join-Path $componentDirectory 'network_manager.c'),
    (Join-Path $componentDirectory 'network_manager_policy.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_config.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_scan.c'),
    (Join-Path $componentDirectory 'network_manager_event_journal.c'),
    (Join-Path $componentDirectory 'network_manager_storage_model.c'),
    (Join-Path $componentDirectory 'network_manager_storage_nvs.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_cellular_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_dual_runtime_model.c')
)
Invoke-HostTest -Name 'test_network_manager_event_priority_host' -ExtraArguments @(
    "-I$(Join-Path $PSScriptRoot 'support\fake')",
    '-DNETWORK_MANAGER_HOST_TEST'
) -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'support\fake_host.c'),
    (Join-Path $PSScriptRoot 'test_network_manager_event_priority_host.c'),
    (Join-Path $componentDirectory 'network_manager.c'),
    (Join-Path $componentDirectory 'network_manager_policy.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_config.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_scan.c'),
    (Join-Path $componentDirectory 'network_manager_event_journal.c'),
    (Join-Path $componentDirectory 'network_manager_storage_model.c'),
    (Join-Path $componentDirectory 'network_manager_storage_nvs.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_cellular_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_dual_runtime_model.c')
)
Invoke-HostTest -Name 'test_network_manager_start_cleanup_host' -ExtraArguments @(
    "-I$(Join-Path $PSScriptRoot 'support\fake')",
    '-DNETWORK_MANAGER_HOST_TEST'
) -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'support\fake_host.c'),
    (Join-Path $PSScriptRoot 'test_network_manager_start_cleanup_host.c'),
    (Join-Path $componentDirectory 'network_manager.c'),
    (Join-Path $componentDirectory 'network_manager_policy.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_config.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_scan.c'),
    (Join-Path $componentDirectory 'network_manager_event_journal.c'),
    (Join-Path $componentDirectory 'network_manager_storage_model.c'),
    (Join-Path $componentDirectory 'network_manager_storage_nvs.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_cellular_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_dual_runtime_model.c')
)
Invoke-HostTest -Name 'test_network_manager_facade_cellular_recovery_host' -ExtraArguments @(
    "-I$(Join-Path $PSScriptRoot 'support\fake')",
    '-DNETWORK_MANAGER_HOST_TEST'
) -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'support\fake_host.c'),
    (Join-Path $PSScriptRoot 'test_network_manager_facade_cellular_recovery_host.c'),
    (Join-Path $componentDirectory 'network_manager.c'),
    (Join-Path $componentDirectory 'network_manager_policy.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_config.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_scan.c'),
    (Join-Path $componentDirectory 'network_manager_event_journal.c'),
    (Join-Path $componentDirectory 'network_manager_storage_model.c'),
    (Join-Path $componentDirectory 'network_manager_storage_nvs.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_cellular_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_dual_runtime_model.c')
)
Invoke-HostTest -Name 'test_network_manager_facade_cellular_event_dual_host' -ExtraArguments @(
    "-I$(Join-Path $PSScriptRoot 'support\fake')",
    '-DNETWORK_MANAGER_HOST_TEST'
) -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'support\fake_host.c'),
    (Join-Path $PSScriptRoot 'test_network_manager_facade_cellular_event_dual_host.c'),
    (Join-Path $componentDirectory 'network_manager.c'),
    (Join-Path $componentDirectory 'network_manager_policy.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_config.c'),
    (Join-Path $componentDirectory 'network_manager_event_journal.c'),
    (Join-Path $componentDirectory 'network_manager_storage_model.c'),
    (Join-Path $componentDirectory 'network_manager_storage_nvs.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_cellular_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_dual_runtime_model.c')
)
Invoke-HostTest -Name 'test_network_manager_facade_wifi_disconnect_report_host' -ExtraArguments @(
    "-I$(Join-Path $PSScriptRoot 'support\fake')",
    '-DNETWORK_MANAGER_HOST_TEST'
) -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'support\fake_host.c'),
    (Join-Path $PSScriptRoot 'test_network_manager_facade_wifi_disconnect_report_host.c'),
    (Join-Path $componentDirectory 'network_manager.c'),
    (Join-Path $componentDirectory 'network_manager_policy.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_config.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_scan.c'),
    (Join-Path $componentDirectory 'network_manager_event_journal.c'),
    (Join-Path $componentDirectory 'network_manager_storage_model.c'),
    (Join-Path $componentDirectory 'network_manager_storage_nvs.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_cellular_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_dual_runtime_model.c')
)
Invoke-HostTest -Name 'test_network_manager_facade_lte_hal_state_host' -ExtraArguments @(
    "-I$(Join-Path $PSScriptRoot 'support\fake')",
    '-DNETWORK_MANAGER_HOST_TEST'
) -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'support\fake_host.c'),
    (Join-Path $PSScriptRoot 'test_network_manager_facade_lte_hal_state_host.c'),
    (Join-Path $componentDirectory 'network_manager.c'),
    (Join-Path $componentDirectory 'network_manager_policy.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_config.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_scan.c'),
    (Join-Path $componentDirectory 'network_manager_event_journal.c'),
    (Join-Path $componentDirectory 'network_manager_storage_model.c'),
    (Join-Path $componentDirectory 'network_manager_storage_nvs.c'),
    (Join-Path $componentDirectory 'network_manager_wifi_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_cellular_runtime_model.c'),
    (Join-Path $componentDirectory 'network_manager_dual_runtime_model.c')
)

Write-Output "clang=$ClangPath"
Write-Output "target=$targetTriple"
Write-Output 'network_manager host tests passed'
