param()

$ErrorActionPreference = 'Stop'
$sampleRate = 16000
$durationMs = 280
$sampleCount = [int]($sampleRate * $durationMs / 1000)
$outputPath = Join-Path $PSScriptRoot '..\..\..\spiffs\merit_tap.wav'
New-Item -ItemType Directory -Path (Split-Path -Parent $outputPath) -Force | Out-Null

$stream = [IO.File]::Open($outputPath, [IO.FileMode]::Create, [IO.FileAccess]::Write)
$writer = [IO.BinaryWriter]::new($stream)
try {
    $dataBytes = $sampleCount * 2
    $writer.Write([Text.Encoding]::ASCII.GetBytes('RIFF'))
    $writer.Write([int](36 + $dataBytes))
    $writer.Write([Text.Encoding]::ASCII.GetBytes('WAVE'))
    $writer.Write([Text.Encoding]::ASCII.GetBytes('fmt '))
    $writer.Write([int]16)
    $writer.Write([int16]1)
    $writer.Write([int16]1)
    $writer.Write([int]$sampleRate)
    $writer.Write([int]($sampleRate * 2))
    $writer.Write([int16]2)
    $writer.Write([int16]16)
    $writer.Write([Text.Encoding]::ASCII.GetBytes('data'))
    $writer.Write([int]$dataBytes)
    for ($sample = 0; $sample -lt $sampleCount; $sample++) {
        $time = $sample / [double]$sampleRate
        $envelope = [Math]::Exp(-18.0 * $time)
        $tone = [Math]::Sin(2.0 * [Math]::PI * 690.0 * $time) * 0.75 +
            [Math]::Sin(2.0 * [Math]::PI * 1380.0 * $time) * 0.25
        $click = if ($sample -lt 18) { [Math]::Exp(-0.35 * $sample) } else { 0.0 }
        $value = [int]([Math]::Max(-1.0, [Math]::Min(1.0,
            ($tone * $envelope) + (0.18 * $click))) * 30000)
        $writer.Write([int16]$value)
    }
} finally {
    $writer.Dispose()
    $stream.Dispose()
}
