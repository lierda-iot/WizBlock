param()

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$assetRoot = $PSScriptRoot
$outputRoot = Join-Path $assetRoot '..\..\..\components\companion_expression\generated'
$outputPath = Join-Path $outputRoot 'companion_merit_tap_assets.c'
$frameNames = @(
    'merit_bubble_01_seed.png',
    'merit_bubble_01a_sprout.png',
    'merit_bubble_01b_growing.png',
    'merit_bubble_02_readable.png',
    'merit_bubble_02a_expand.png',
    'merit_bubble_03_fade_max.png',
    'merit_bubble_03a_dissolve.png'
)
$originX = 140
$originY = 0
$tileWidth = 180
$tileHeight = 110

function Convert-ToRgb565 {
    param([Parameter(Mandatory = $true)][Drawing.Color]$Color)
    return (($Color.R -shr 3) -shl 11) -bor
           (($Color.G -shr 2) -shl 5) -bor
           ($Color.B -shr 3)
}

function Add-Values {
    param(
        [Parameter(Mandatory = $true)][Text.StringBuilder]$Builder,
        [Parameter(Mandatory = $true)][string]$Type,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)]$Values,
        [Parameter(Mandatory = $true)][string]$Format
    )
    [void]$Builder.AppendLine("static const ${Type} ${Name}[] = {")
    for ($offset = 0; $offset -lt $Values.Count; $offset += 16) {
        $limit = [Math]::Min($offset + 16, $Values.Count)
        $line = for ($index = $offset; $index -lt $limit; $index++) {
            $Format -f $Values[$index]
        }
        [void]$Builder.AppendLine('    ' + ($line -join ', ') + ',')
    }
    [void]$Builder.AppendLine('};')
    [void]$Builder.AppendLine()
}

New-Item -ItemType Directory -Path $outputRoot -Force | Out-Null
$builder = [Text.StringBuilder]::new()
[void]$builder.AppendLine('#include "companion_merit_tap_assets.h"')
[void]$builder.AppendLine()
$assetNames = @()
try {
    foreach ($frameName in $frameNames) {
        $path = Join-Path $assetRoot $frameName
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Missing approved merit frame: $path"
        }
        $bitmap = [Drawing.Bitmap]::new($path)
        try {
            if (320 -ne $bitmap.Width -or 240 -ne $bitmap.Height) {
                throw "Unexpected merit frame size ${frameName}: $($bitmap.Width)x$($bitmap.Height)"
            }
            $pixels = [Collections.Generic.List[int]]::new()
            $alphas = [Collections.Generic.List[int]]::new()
            for ($y = $originY; $y -lt ($originY + $tileHeight); $y++) {
                for ($x = $originX; $x -lt ($originX + $tileWidth); $x++) {
                    $color = $bitmap.GetPixel($x, $y)
                    $maxChannel = [Math]::Max($color.R, [Math]::Max($color.G, $color.B))
                    $isYellowOrWhite = $maxChannel -ge 12 -and
                        $color.R -ge $color.B -and $color.G -ge $color.B
                    if ($isYellowOrWhite) {
                        $pixels.Add((Convert-ToRgb565 -Color $color))
                        $alphas.Add([Math]::Min(255, $maxChannel))
                    } else {
                        $pixels.Add(0)
                        $alphas.Add(0)
                    }
                }
            }
            $identifier = [regex]::Replace(
                [IO.Path]::GetFileNameWithoutExtension($frameName),
                '[^A-Za-z0-9_]', '_')
            Add-Values -Builder $builder -Type 'uint16_t' -Name "s_${identifier}_pixels" `
                -Values $pixels -Format '0x{0:X4}'
            Add-Values -Builder $builder -Type 'uint8_t' -Name "s_${identifier}_alpha" `
                -Values $alphas -Format '0x{0:X2}'
            $assetNames += $identifier
        } finally {
            $bitmap.Dispose()
        }
    }
    [void]$builder.AppendLine('const companion_merit_bubble_asset_t g_companion_merit_bubble_assets[] = {')
    foreach ($assetName in $assetNames) {
        [void]$builder.AppendLine("    { s_${assetName}_pixels, s_${assetName}_alpha },")
    }
    [void]$builder.AppendLine('};')
    [IO.File]::WriteAllText($outputPath, $builder.ToString(), [Text.Encoding]::ASCII)
} catch {
    throw
}
