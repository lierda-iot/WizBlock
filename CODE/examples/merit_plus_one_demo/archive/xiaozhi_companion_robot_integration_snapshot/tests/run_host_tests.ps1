param()

$ErrorActionPreference = "Stop"

$testRoot = $PSScriptRoot
$exampleRoot = Split-Path -Parent $testRoot
$buildRoot = Join-Path $testRoot "build"
$coreIncludeRoot = Join-Path $exampleRoot "components\companion_core\include"
$logicIncludeRoot = Join-Path $exampleRoot "components\companion_logic\include"
$meritIncludeRoot = Join-Path $exampleRoot "components\companion_merit_tap\include"
$controllerIncludeRoot = Join-Path $exampleRoot "components\companion_controller\include"
$doaIncludeRoot = Join-Path $exampleRoot "components\companion_doa\include"
$touchIncludeRoot = Join-Path $exampleRoot "components\companion_touch_gesture\include"
$turnIncludeRoot = Join-Path $exampleRoot "components\companion_turn_control\include"
$networkIncludeRoot = Join-Path $exampleRoot "components\companion_network\include"
$supportRoot = Join-Path $testRoot "support"
$coreSource = Join-Path $exampleRoot "components\companion_core\companion_core.c"
$logicSource = Join-Path $exampleRoot "components\companion_logic\companion_logic.c"
$meritSource = Join-Path $exampleRoot "components\companion_merit_tap\companion_merit_tap.c"
$controllerModelSource = Join-Path $exampleRoot "components\companion_controller\companion_controller_model.c"
$doaEstimatorSource = Join-Path $exampleRoot "components\companion_doa\companion_doa_estimator.c"
$touchSource = Join-Path $exampleRoot "components\companion_touch_gesture\companion_touch_gesture.c"
$turnSource = Join-Path $exampleRoot "components\companion_turn_control\companion_turn_control.c"
$networkPolicySource = Join-Path $exampleRoot "components\companion_network\companion_network_policy.c"
$caseSource = Join-Path $testRoot "companion_core_test_cases.c"
$mainSource = Join-Path $testRoot "companion_core_host_main.c"
$output = Join-Path $buildRoot "companion_core_test.exe"

$compiler = Get-Command gcc, clang, cl -ErrorAction SilentlyContinue |
    Select-Object -First 1
if ($null -eq $compiler) {
    throw "No supported host compiler found. Use run_target_self_test.ps1."
}

New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null

if ("cl.exe" -eq $compiler.Name -or "cl" -eq $compiler.Name) {
    & $compiler.Source /nologo /std:c11 /W4 /WX `
        "/I$supportRoot" "/I$coreIncludeRoot" "/I$logicIncludeRoot" `
        "/I$meritIncludeRoot" `
        "/I$controllerIncludeRoot" "/I$doaIncludeRoot" "/I$touchIncludeRoot" `
        "/I$turnIncludeRoot" "/I$networkIncludeRoot" "/I$testRoot" `
        "/Fe:$output" $coreSource $logicSource $controllerModelSource `
        $meritSource $doaEstimatorSource $touchSource $turnSource $networkPolicySource `
        $caseSource $mainSource
} else {
    & $compiler.Source -std=c11 -Wall -Wextra -Werror `
        "-I$supportRoot" "-I$coreIncludeRoot" "-I$logicIncludeRoot" `
        "-I$meritIncludeRoot" `
        "-I$controllerIncludeRoot" "-I$doaIncludeRoot" "-I$touchIncludeRoot" `
        "-I$turnIncludeRoot" "-I$networkIncludeRoot" "-I$testRoot" `
        -o $output $coreSource $logicSource $controllerModelSource `
        $meritSource $doaEstimatorSource $touchSource $turnSource $networkPolicySource `
        $caseSource $mainSource
}
if (0 -ne $LASTEXITCODE) {
    throw "Host test compilation failed with exit code $LASTEXITCODE."
}

& $output
if (0 -ne $LASTEXITCODE) {
    throw "Host tests failed with exit code $LASTEXITCODE."
}
