param(
    [string]$ClangPath,
    [string]$TestName
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

$mainDirectory = Join-Path (Split-Path -Parent $PSScriptRoot) 'main'
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
    "-I$mainDirectory"
)

function Invoke-HostTest {
    param(
        [string]$Name,
        [string[]]$Sources,
        [string[]]$ExtraArguments = @()
    )

    if ($TestName -and $TestName -ne $Name) {
        return
    }

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
Invoke-HostTest -Name 'test_camera_hal_policy' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_camera_hal_policy.c'),
    (Join-Path $PSScriptRoot '..\..\..\components\camera_hal\camera_hal_policy.c')
) -ExtraArguments @("-I$(Join-Path $PSScriptRoot '..\..\..\components\camera_hal\include')")

Invoke-HostTest -Name 'test_rc_net_stream' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_rc_net_stream.c')
)

Invoke-HostTest -Name 'test_rc_net_send_policy' -Sources @(
    $freestandingSupport,
    (Join-Path $mainDirectory 'rc_net_send_policy.c'),
    (Join-Path $PSScriptRoot 'test_rc_net_send_policy.c')
)

Invoke-HostTest -Name 'test_rc_video_buffer_select' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_rc_video_buffer_select.c')
)

Invoke-HostTest -Name 'test_rc_capture_pool' -Sources @(
    $freestandingSupport,
    (Join-Path $mainDirectory 'rc_capture_pool.c'),
    (Join-Path $PSScriptRoot 'test_rc_capture_pool.c')
)

Invoke-HostTest -Name 'test_rc_capture_pool_target' -Sources @(
    $freestandingSupport,
    (Join-Path $mainDirectory 'rc_capture_pool.c'),
    (Join-Path $mainDirectory 'rc_capture_pool_target.c'),
    (Join-Path $PSScriptRoot 'test_rc_capture_pool_target.c')
)

Invoke-HostTest -Name 'test_rc_video_scale' -Sources @(
    $freestandingSupport,
    (Join-Path $mainDirectory 'rc_video_scale.c'),
    (Join-Path $PSScriptRoot 'test_rc_video_scale.c')
)

Invoke-HostTest -Name 'test_rc_video_yuv_scale' -Sources @(
    $freestandingSupport,
    (Join-Path $mainDirectory 'rc_video_yuv_scale.c'),
    (Join-Path $PSScriptRoot 'test_rc_video_yuv_scale.c')
)

Invoke-HostTest -Name 'test_rc_video_format' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_rc_video_format.c')
)

Invoke-HostTest -Name 'test_rc_video_controller_policy' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_rc_video_controller_policy.c')
)

Invoke-HostTest -Name 'test_rc_video_udp_transport' -Sources @(
    $freestandingSupport,
    (Join-Path $mainDirectory 'rc_video_udp_transport.c'),
    (Join-Path $PSScriptRoot 'test_rc_video_udp_transport.c')
) -ExtraArguments @('-DCONFIG_RC_TANK_ROLE_REMOTE=1')

Invoke-HostTest -Name 'test_rc_display_orientation' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_rc_display_orientation.c')
)

Invoke-HostTest -Name 'test_rc_remote_display_policy' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_rc_remote_display_policy.c')
)

Invoke-HostTest -Name 'test_rc_control_tx_policy' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_rc_control_tx_policy.c')
)

Invoke-HostTest -Name 'test_rc_ctrl_protocol' -Sources @(
    $freestandingSupport,
    (Join-Path $mainDirectory 'rc_ctrl_protocol.c'),
    (Join-Path $PSScriptRoot 'test_rc_ctrl_protocol.c')
) -ExtraArguments @('-DCONFIG_RC_TANK_ROLE_REMOTE=1')

Invoke-HostTest -Name 'test_rc_drive_control' -Sources @(
    $freestandingSupport,
    (Join-Path $mainDirectory 'rc_drive_control.c'),
    (Join-Path $PSScriptRoot 'test_rc_drive_control.c')
) -ExtraArguments @('-DCONFIG_RC_TANK_ROLE_TANK=1')

# Link real rc_joystick.c (define REMOTE role to pass rc_tank_common.h role gate)
$joystickSources = @(
    $freestandingSupport,
    (Join-Path $mainDirectory 'rc_joystick.c'),
    (Join-Path $PSScriptRoot 'test_rc_joystick_direction.c')
)
$joystickArgs = @('-DCONFIG_RC_TANK_ROLE_REMOTE=1')
Invoke-HostTest -Name 'test_rc_joystick_direction' -Sources $joystickSources -ExtraArguments $joystickArgs

Invoke-HostTest -Name 'test_rc_video_display_plan' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_rc_video_display_plan.c')
)

Invoke-HostTest -Name 'test_rc_video_latest_frame' -Sources @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_rc_video_latest_frame.c')
)

# Link real rc_tank_screen.c (pure geometry, no ESP-IDF deps)
$tankScreenSources = @(
    $freestandingSupport,
    (Join-Path $PSScriptRoot 'test_rc_tank_screen_render.c')
)
Invoke-HostTest -Name 'test_rc_tank_screen_render' -Sources $tankScreenSources

# Link real rc_rle.c (pure C RLE codec, no ESP-IDF deps)
$rleSources = @(
    $freestandingSupport,
    (Join-Path $mainDirectory 'rc_rle.c'),
    (Join-Path $PSScriptRoot 'test_rc_rle.c')
)
Invoke-HostTest -Name 'test_rc_rle' -Sources $rleSources

Write-Output "clang=$ClangPath"
Write-Output "target=$targetTriple"
Write-Output 'rc_tank_demo host tests passed'
