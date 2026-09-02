param(
    [string]$Port
)

$ErrorActionPreference = "Stop"

$exampleName = "xiaozhi_companion_robot_demo"
$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..\..\..")).Path

$bash = "C:\Program Files\Git\bin\bash.exe"
if (-not (Test-Path -LiteralPath $bash)) {
    throw "Git Bash not found at the documented path: $bash"
}

$repoPosix = $repoRoot.Replace('\', '/')
$tempPosix = $env:TEMP.Replace('\', '/')
$mirrorParent = "$tempPosix/laiwfs300_build"
$mirrorCode = "$mirrorParent/CODE"
$mirrorCommand = "mkdir -p `"$mirrorParent`" && rm -rf `"$mirrorCode`" && cp -r `"$repoPosix/CODE`" `"$mirrorCode`""

& $bash -lc $mirrorCommand
if (0 -ne $LASTEXITCODE) {
    throw "Full CODE mirror failed with exit code $LASTEXITCODE."
}

$buildScript = Join-Path $repoRoot "CODE\tools\build_example.ps1"
$python = "D:\Espressif\python_env\idf5.5_py3.11_env\Scripts\python.exe"
$previousSelfTest = $env:COMPANION_SELF_TEST
$previousPath = $env:Path
$env:COMPANION_SELF_TEST = "1"
$pathWithoutGit = foreach ($pathEntry in ($env:Path -split ';')) {
    if ([string]::IsNullOrWhiteSpace($pathEntry)) {
        continue
    }
    if (-not (Test-Path -LiteralPath (Join-Path $pathEntry "git.exe"))) {
        $pathEntry
    }
}
$env:Path = $pathWithoutGit -join ';'

try {
    if ($null -ne (Get-Command git.exe -ErrorAction SilentlyContinue)) {
        throw "Git executable is still discoverable after PATH isolation."
    }

    & $buildScript -Example $exampleName -Clean
    if (0 -ne $LASTEXITCODE) {
        throw "Target self-test clean build failed with exit code $LASTEXITCODE."
    }

    if (-not [string]::IsNullOrWhiteSpace($Port)) {
        & $python -m esptool --chip esp32s3 -p $Port erase_flash
        if (0 -ne $LASTEXITCODE) {
            throw "Full-chip erase failed with exit code $LASTEXITCODE."
        }

        & $buildScript -Example $exampleName -Port $Port
        if (0 -ne $LASTEXITCODE) {
            throw "Target self-test flash failed with exit code $LASTEXITCODE."
        }
    }
} finally {
    $env:Path = $previousPath
    if ($null -eq $previousSelfTest) {
        Remove-Item Env:COMPANION_SELF_TEST -ErrorAction SilentlyContinue
    } else {
        $env:COMPANION_SELF_TEST = $previousSelfTest
    }
}
