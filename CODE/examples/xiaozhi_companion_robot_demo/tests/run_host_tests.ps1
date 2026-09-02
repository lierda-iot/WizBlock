param()

$ErrorActionPreference = "Stop"

$testRoot = $PSScriptRoot
$exampleRoot = Split-Path -Parent $testRoot
$buildRoot = Join-Path $testRoot "build"
$coreIncludeRoot = Join-Path $exampleRoot "components\companion_core\include"
$logicIncludeRoot = Join-Path $exampleRoot "components\companion_logic\include"
$controllerIncludeRoot = Join-Path $exampleRoot "components\companion_controller\include"
$doaIncludeRoot = Join-Path $exampleRoot "components\companion_doa\include"
$touchIncludeRoot = Join-Path $exampleRoot "components\companion_touch_gesture\include"
$turnIncludeRoot = Join-Path $exampleRoot "components\companion_turn_control\include"
$motionIncludeRoot = Join-Path $exampleRoot "components\companion_motion\include"
$agentIncludeRoot = Join-Path (Split-Path -Parent (Split-Path -Parent $exampleRoot)) "components\xiaozhi_agent\include"
$audioProcessorIncludeRoot = Join-Path (Split-Path -Parent (Split-Path -Parent $exampleRoot)) "components\audio_processor\include"
$audioIncludeRoot = Join-Path $exampleRoot "components\companion_audio\include"
$adapterIncludeRoot = Join-Path $exampleRoot "components\companion_agent_adapter\include"
$supportRoot = Join-Path $testRoot "support"
$coreSource = Join-Path $exampleRoot "components\companion_core\companion_core.c"
$logicSource = Join-Path $exampleRoot "components\companion_logic\companion_logic.c"
$controllerModelSource = Join-Path $exampleRoot "components\companion_controller\companion_controller_model.c"
$controllerRuntimePolicySource = Join-Path $exampleRoot "components\companion_controller\companion_controller_runtime_policy.c"
$controllerStopPolicySource = Join-Path $exampleRoot "components\companion_controller\companion_controller_stop_policy.c"
$controllerWakeEffectsSource = Join-Path $exampleRoot "components\companion_controller\companion_controller_wake_effects.c"
$doaEstimatorSource = Join-Path $exampleRoot "components\companion_doa\companion_doa_estimator.c"
$touchSource = Join-Path $exampleRoot "components\companion_touch_gesture\companion_touch_gesture.c"
$turnSource = Join-Path $exampleRoot "components\companion_turn_control\companion_turn_control.c"
$motionResultPolicySource = Join-Path $exampleRoot "components\companion_motion\companion_motion_result_policy.c"
$wsStartPolicySource = Join-Path (Split-Path -Parent (Split-Path -Parent $exampleRoot)) "components\xiaozhi_agent\xiaozhi_agent_ws_start_policy.c"
$listenModePolicySource = Join-Path (Split-Path -Parent (Split-Path -Parent $exampleRoot)) "components\xiaozhi_agent\xiaozhi_agent_listen_mode_policy.c"
$ttsBarrierPolicySource = Join-Path (Split-Path -Parent (Split-Path -Parent $exampleRoot)) "components\xiaozhi_agent\xiaozhi_agent_tts_barrier_policy.c"
$vadStopPolicySource = Join-Path (Split-Path -Parent (Split-Path -Parent $exampleRoot)) "components\xiaozhi_agent\xiaozhi_agent_vad_stop_policy.c"
$audioProcessorPolicySource = Join-Path (Split-Path -Parent (Split-Path -Parent $exampleRoot)) "components\audio_processor\audio_processor_policy.c"
$audioProcessorTaskPolicySource = Join-Path (Split-Path -Parent (Split-Path -Parent $exampleRoot)) "components\audio_processor\audio_processor_task_policy.c"
$companionAudioProcessorPolicySource = Join-Path $exampleRoot "components\companion_audio\companion_audio_processor_policy.c"
$vadPolicySource = Join-Path $exampleRoot "components\companion_audio\companion_audio_vad_policy.c"
$voiceGateSource = Join-Path $exampleRoot "components\companion_audio\companion_audio_voice_gate.c"
$pcmQueueSource = Join-Path $exampleRoot "components\companion_audio\companion_audio_pcm_queue.c"
$bindingPolicySource = Join-Path $exampleRoot "components\companion_agent_adapter\companion_agent_binding_policy.c"
$caseSource = Join-Path $testRoot "companion_core_test_cases.c"
$signalMetricsSource = Join-Path $testRoot "companion_audio_signal_metrics.c"
$mainSource = Join-Path $testRoot "companion_core_host_main.c"
$output = Join-Path $buildRoot "companion_core_test.exe"

$compiler = Get-Command gcc, clang, cl -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($null -eq $compiler) {
    $bundledClang = Join-Path $env:LOCALAPPDATA "LLVM\22.1.8\bin\clang.exe"
    if (Test-Path -LiteralPath $bundledClang) {
        $compiler = Get-Item -LiteralPath $bundledClang
    }
}
if ($null -eq $compiler) {
    throw "No supported host compiler found. Use run_target_self_test.ps1."
}

New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null

if ("cl.exe" -eq $compiler.Name -or "cl" -eq $compiler.Name) {
    & $compiler.Source /nologo /std:c11 /W4 /WX `
        "/I$supportRoot" "/I$coreIncludeRoot" "/I$logicIncludeRoot" `
        "/I$controllerIncludeRoot" "/I$doaIncludeRoot" "/I$touchIncludeRoot" `
        "/I$turnIncludeRoot" "/I$motionIncludeRoot" "/I$agentIncludeRoot" `
        "/I$audioProcessorIncludeRoot" "/I$audioIncludeRoot" `
        "/I$adapterIncludeRoot" "/I$testRoot" `
        "/Fe:$output" $coreSource $logicSource $controllerModelSource `
        $controllerRuntimePolicySource `
        $controllerStopPolicySource $controllerWakeEffectsSource `
        $doaEstimatorSource $touchSource $turnSource $motionResultPolicySource `
        $wsStartPolicySource $listenModePolicySource `
        $ttsBarrierPolicySource $vadPolicySource $voiceGateSource `
        $vadStopPolicySource $audioProcessorPolicySource `
        $audioProcessorTaskPolicySource `
        $companionAudioProcessorPolicySource `
        $pcmQueueSource $bindingPolicySource `
        $signalMetricsSource $caseSource $mainSource
} elseif ("clang.exe" -eq $compiler.Name -or "clang" -eq $compiler.Name) {
    $compilerRoot = Split-Path -Parent $compiler.FullName
    $dllTool = Join-Path $compilerRoot "llvm-dlltool.exe"
    if (-not (Test-Path -LiteralPath $dllTool)) {
        throw "LLVM dlltool not found beside clang: $dllTool"
    }
    $msvcrtLib = Join-Path $buildRoot "msvcrt.lib"
    $ucrtbaseLib = Join-Path $buildRoot "ucrtbase.lib"
    $kernel32Lib = Join-Path $buildRoot "kernel32.lib"
    & $dllTool -m i386:x86-64 -d (Join-Path $supportRoot "msvcrt.def") -l $msvcrtLib
    & $dllTool -m i386:x86-64 -d (Join-Path $supportRoot "ucrtbase.def") -l $ucrtbaseLib
    & $dllTool -m i386:x86-64 -d (Join-Path $supportRoot "kernel32.def") -l $kernel32Lib
    if (0 -ne $LASTEXITCODE) {
        throw "LLVM import library generation failed with exit code $LASTEXITCODE."
    }
    & $compiler.FullName -std=c11 -Wall -Wextra -Werror -nostdlib `
        -fno-stack-protector -DCOMPANION_HOST_NO_CRT `
        "-I$supportRoot" "-I$coreIncludeRoot" "-I$logicIncludeRoot" `
        "-I$controllerIncludeRoot" "-I$doaIncludeRoot" "-I$touchIncludeRoot" `
        "-I$turnIncludeRoot" "-I$motionIncludeRoot" "-I$agentIncludeRoot" `
        "-I$audioProcessorIncludeRoot" "-I$audioIncludeRoot" `
        "-I$adapterIncludeRoot" "-I$testRoot" `
        -fuse-ld=lld -Xlinker /entry:mainCRTStartup `
        -Xlinker /subsystem:console -Xlinker /nodefaultlib `
        -o $output $coreSource $logicSource $controllerModelSource `
        $controllerRuntimePolicySource `
        $controllerStopPolicySource $controllerWakeEffectsSource `
        $doaEstimatorSource $touchSource $turnSource $motionResultPolicySource `
        $wsStartPolicySource $listenModePolicySource `
        $ttsBarrierPolicySource $vadPolicySource $voiceGateSource `
        $vadStopPolicySource $audioProcessorPolicySource `
        $audioProcessorTaskPolicySource `
        $companionAudioProcessorPolicySource `
        $pcmQueueSource $bindingPolicySource $signalMetricsSource `
        $caseSource $mainSource `
        $msvcrtLib $ucrtbaseLib $kernel32Lib
} else {
    & $compiler.Source -std=c11 -Wall -Wextra -Werror `
        "-I$supportRoot" "-I$coreIncludeRoot" "-I$logicIncludeRoot" `
        "-I$controllerIncludeRoot" "-I$doaIncludeRoot" "-I$touchIncludeRoot" `
        "-I$turnIncludeRoot" "-I$motionIncludeRoot" "-I$agentIncludeRoot" `
        "-I$audioProcessorIncludeRoot" "-I$audioIncludeRoot" `
        "-I$adapterIncludeRoot" "-I$testRoot" `
        -o $output $coreSource $logicSource $controllerModelSource `
        $controllerRuntimePolicySource `
        $controllerStopPolicySource $controllerWakeEffectsSource `
        $doaEstimatorSource $touchSource $turnSource $motionResultPolicySource `
        $wsStartPolicySource $listenModePolicySource `
        $ttsBarrierPolicySource $vadPolicySource $voiceGateSource `
        $vadStopPolicySource $audioProcessorPolicySource `
        $audioProcessorTaskPolicySource `
        $companionAudioProcessorPolicySource `
        $pcmQueueSource $bindingPolicySource `
        $signalMetricsSource $caseSource $mainSource
}
if (0 -ne $LASTEXITCODE) {
    throw "Host test compilation failed with exit code $LASTEXITCODE."
}

& $output
if (0 -ne $LASTEXITCODE) {
    throw "Host tests failed with exit code $LASTEXITCODE."
}
