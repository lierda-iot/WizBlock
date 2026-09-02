param()

$ErrorActionPreference = "Stop"

$testRoot = $PSScriptRoot
$exampleRoot = Split-Path -Parent $testRoot
$logicRoot = Join-Path $exampleRoot "main"
$supportRoot = Join-Path $testRoot "support"
$buildRoot = Join-Path $testRoot "build"
$logicSource = Join-Path $logicRoot "aec_test_logic.c"
$testSource = Join-Path $testRoot "aec_test_logic_test.c"
$output = Join-Path $buildRoot "aec_test_logic_test.exe"

$compiler = Get-Command clang -ErrorAction SilentlyContinue
if ($null -eq $compiler) {
    $bundledClang = Join-Path $env:LOCALAPPDATA "LLVM\22.1.8\bin\clang.exe"
    if (Test-Path -LiteralPath $bundledClang) {
        $compiler = Get-Item -LiteralPath $bundledClang
    }
}
if ($null -eq $compiler) {
    throw "LLVM clang was not found."
}

$compilerPath = if ($null -ne $compiler.Source) { $compiler.Source } else { $compiler.FullName }
$compilerRoot = Split-Path -Parent $compilerPath
$dllTool = Join-Path $compilerRoot "llvm-dlltool.exe"
if (-not (Test-Path -LiteralPath $dllTool)) {
    throw "LLVM dlltool not found beside clang: $dllTool"
}

New-Item -ItemType Directory -Path $buildRoot -Force | Out-Null

$msvcrtLib = Join-Path $buildRoot "msvcrt.lib"
$ucrtbaseLib = Join-Path $buildRoot "ucrtbase.lib"
$kernel32Lib = Join-Path $buildRoot "kernel32.lib"
& $dllTool -m i386:x86-64 -d (Join-Path $supportRoot "msvcrt.def") -l $msvcrtLib
& $dllTool -m i386:x86-64 -d (Join-Path $supportRoot "ucrtbase.def") -l $ucrtbaseLib
& $dllTool -m i386:x86-64 -d (Join-Path $supportRoot "kernel32.def") -l $kernel32Lib
if (0 -ne $LASTEXITCODE) {
    throw "LLVM import library generation failed with exit code $LASTEXITCODE."
}

& $compilerPath -std=c11 -Wall -Wextra -Werror -nostdlib `
    -fno-stack-protector -DAEC_TEST_HOST_NO_CRT `
    "-I$supportRoot" "-I$logicRoot" -fuse-ld=lld -Xlinker /entry:mainCRTStartup `
    -Xlinker /subsystem:console -Xlinker /nodefaultlib `
    -o $output $logicSource $testSource $msvcrtLib $ucrtbaseLib $kernel32Lib
if (0 -ne $LASTEXITCODE) {
    throw "Host test compilation failed with exit code $LASTEXITCODE."
}

& $output
if (0 -ne $LASTEXITCODE) {
    throw "Host tests failed with exit code $LASTEXITCODE."
}
