param()

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$LogicalWidth = 80
$LogicalHeight = 60
$PreviewScale = 12
$FrameBytes = 2400
$PaletteLimit = 16
$ManifestPath = Join-Path $PSScriptRoot 'expression_manifest.psd1'
$ConceptRoot = Join-Path $PSScriptRoot 'concepts'
$PackOutputRoot = Join-Path $PSScriptRoot 'packs'
$ComponentRoot = Join-Path $PSScriptRoot '..\..\components\companion_expression'
$GeneratedSource = Join-Path $ComponentRoot 'generated\companion_expression_assets.c'
$TempRoot = Join-Path $PSScriptRoot ('.asset_generation_' + [guid]::NewGuid().ToString('N'))
$TempPacks = Join-Path $TempRoot 'packs'
$TempSource = Join-Path $TempRoot 'companion_expression_assets.c'

function Get-Fnv1a32 {
    param([Parameter(Mandatory = $true)][string]$Value)

    [uint32]$hash = 2166136261
    foreach ($byte in [Text.Encoding]::ASCII.GetBytes($Value)) {
        $hash = [uint32]($hash -bxor [uint32]$byte)
        $hash = [uint32](([uint64]$hash * 16777619) -band 0xFFFFFFFFL)
    }
    return $hash
}

function Convert-ToIdentifier {
    param([Parameter(Mandatory = $true)][string]$Value)

    return [regex]::Replace($Value.ToLowerInvariant(), '[^a-z0-9_]', '_')
}

function Convert-ToRgb565 {
    param([Parameter(Mandatory = $true)][int]$Rgb)

    $red = ($Rgb -shr 16) -band 0xFF
    $green = ($Rgb -shr 8) -band 0xFF
    $blue = $Rgb -band 0xFF
    return (($red -shr 3) -shl 11) -bor
           (($green -shr 2) -shl 5) -bor
           ($blue -shr 3)
}

function Add-ByteArray {
    param(
        [Parameter(Mandatory = $true)][Text.StringBuilder]$Builder,
        [Parameter(Mandatory = $true)][string]$Name,
        [Parameter(Mandatory = $true)][byte[]]$Bytes
    )

    [void]$Builder.AppendLine("static const uint8_t ${Name}[] = {")
    for ($offset = 0; $offset -lt $Bytes.Count; $offset += 16) {
        $limit = [Math]::Min($offset + 16, $Bytes.Count)
        $values = for ($index = $offset; $index -lt $limit; $index++) {
            '0x{0:X2}' -f $Bytes[$index]
        }
        [void]$Builder.AppendLine('    ' + ($values -join ', ') + ',')
    }
    [void]$Builder.AppendLine('};')
    [void]$Builder.AppendLine()
}

function Assert-UniqueIds {
    param(
        [Parameter(Mandatory = $true)]$Items,
        [Parameter(Mandatory = $true)][string]$Property,
        [Parameter(Mandatory = $true)][string]$Kind
    )

    $names = @{}
    $hashes = @{}
    foreach ($item in $Items) {
        $name = [string]$item[$Property]
        if ([string]::IsNullOrWhiteSpace($name) -or $names.ContainsKey($name)) {
            throw "Duplicate or empty $Kind id: '$name'"
        }
        $hash = Get-Fnv1a32 -Value $name
        if ($hashes.ContainsKey($hash)) {
            throw "FNV-1a collision for $Kind '$name' and '$($hashes[$hash])'"
        }
        $names[$name] = $true
        $hashes[$hash] = $name
    }
}

function Assert-Fallbacks {
    param([Parameter(Mandatory = $true)]$Scenes)

    $byName = @{}
    foreach ($scene in $Scenes) {
        $byName[[string]$scene.SceneId] = $scene
    }
    foreach ($scene in $Scenes) {
        $visited = @{}
        $current = $scene
        while ($null -ne $current.Fallback) {
            $fallback = [string]$current.Fallback
            if (-not $byName.ContainsKey($fallback)) {
                throw "Scene '$($scene.SceneId)' has unknown fallback '$fallback'"
            }
            if ($visited.ContainsKey($fallback) -or $fallback -eq $scene.SceneId) {
                throw "Fallback cycle starts at scene '$($scene.SceneId)'"
            }
            $visited[$fallback] = $true
            $current = $byName[$fallback]
        }
    }
}

$manifest = Import-PowerShellDataFile -LiteralPath $ManifestPath
$packs = @($manifest.Packs | Sort-Object { [int]$_.Order })
$scenes = @($manifest.Scenes)
if (0 -eq $packs.Count -or 0 -eq $scenes.Count) {
    throw 'Manifest must contain at least one pack and one scene'
}
Assert-UniqueIds -Items $packs -Property 'PackId' -Kind 'pack'
Assert-UniqueIds -Items $scenes -Property 'SceneId' -Kind 'scene'
Assert-Fallbacks -Scenes $scenes
if ($manifest.DefaultPackId -notin @($packs | ForEach-Object { $_.PackId })) {
    throw "Default pack '$($manifest.DefaultPackId)' is not registered"
}

$resolvedTempRoot = [IO.Path]::GetFullPath($TempRoot)
$resolvedExpressionRoot = [IO.Path]::GetFullPath($PSScriptRoot)
if (-not $resolvedTempRoot.StartsWith($resolvedExpressionRoot,
                                      [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Temporary output escaped the expression directory'
}

New-Item -ItemType Directory -Path $TempPacks -Force | Out-Null
$builder = [Text.StringBuilder]::new()
[void]$builder.AppendLine('#include "companion_expression_catalog.h"')
[void]$builder.AppendLine()
$packResults = @()

try {
    foreach ($pack in $packs) {
        $packId = [string]$pack.PackId
        $packIdentifier = Convert-ToIdentifier -Value $packId
        $palette = [Collections.Generic.List[int]]::new()
        $paletteIndex = [Collections.Generic.Dictionary[int, int]]::new()
        $frameResults = @()
        $packPreviewRoot = Join-Path $TempPacks $packId
        New-Item -ItemType Directory -Path $packPreviewRoot -Force | Out-Null

        foreach ($scene in $scenes) {
            $sceneId = [string]$scene.SceneId
            $sourceName = "${packId}_state_${sceneId}_preview_3x.png"
            $sourcePath = Join-Path (Join-Path $ConceptRoot $packId) $sourceName
            if (-not (Test-Path -LiteralPath $sourcePath)) {
                if ([bool]$scene.Required) {
                    throw "Missing required frame '$sourcePath'"
                }
                continue
            }

            $bitmap = [Drawing.Bitmap]::new($sourcePath)
            $preview = $null
            try {
                if (($LogicalWidth * $PreviewScale) -ne $bitmap.Width -or
                    ($LogicalHeight * $PreviewScale) -ne $bitmap.Height) {
                    throw "Unexpected frame size ${sourceName}: $($bitmap.Width)x$($bitmap.Height)"
                }
                $preview = [Drawing.Bitmap]::new($LogicalWidth, $LogicalHeight)
                $indices = [byte[]]::new($LogicalWidth * $LogicalHeight)
                for ($y = 0; $y -lt $LogicalHeight; $y++) {
                    for ($x = 0; $x -lt $LogicalWidth; $x++) {
                        $color = $bitmap.GetPixel(
                            $x * $PreviewScale + [int]($PreviewScale / 2),
                            $y * $PreviewScale + [int]($PreviewScale / 2))
                        $rgb = $color.ToArgb() -band 0x00FFFFFF
                        if (-not $paletteIndex.ContainsKey($rgb)) {
                            if ($PaletteLimit -le $palette.Count) {
                                throw "Pack '$packId' exceeds $PaletteLimit colors"
                            }
                            $paletteIndex[$rgb] = $palette.Count
                            $palette.Add($rgb)
                        }
                        $logicalIndex = $y * $LogicalWidth + $x
                        $indices[$logicalIndex] = [byte]$paletteIndex[$rgb]
                        $preview.SetPixel(
                            $x, $y,
                            [Drawing.Color]::FromArgb(
                                255,
                                ($rgb -shr 16) -band 0xFF,
                                ($rgb -shr 8) -band 0xFF,
                                $rgb -band 0xFF))
                    }
                }
                $previewPath = Join-Path $packPreviewRoot "${packId}_${sceneId}_80x60.png"
                $preview.Save($previewPath, [Drawing.Imaging.ImageFormat]::Png)

                $packed = [byte[]]::new($FrameBytes)
                for ($pixel = 0; $pixel -lt $indices.Count; $pixel += 2) {
                    $packed[[int]($pixel / 2)] = [byte](
                        $indices[$pixel] -bor ($indices[$pixel + 1] -shl 4))
                }
                $arrayName = "s_${packIdentifier}_$(Convert-ToIdentifier -Value $sceneId)"
                Add-ByteArray -Builder $builder -Name $arrayName -Bytes $packed
                $frameResults += [pscustomobject]@{
                    SceneId = $sceneId
                    ArrayName = $arrayName
                }
            }
            finally {
                if ($null -ne $preview) {
                    $preview.Dispose()
                }
                $bitmap.Dispose()
            }
        }

        $paletteName = "s_${packIdentifier}_palette"
        [void]$builder.AppendLine("static const uint16_t ${paletteName}[] = {")
        $paletteValues = foreach ($rgb in $palette) {
            '0x{0:X4}' -f (Convert-ToRgb565 -Rgb $rgb)
        }
        [void]$builder.AppendLine('    ' + ($paletteValues -join ', ') + ',')
        [void]$builder.AppendLine('};')
        [void]$builder.AppendLine()

        $framesName = "s_${packIdentifier}_frames"
        [void]$builder.AppendLine("static const companion_expression_frame_t ${framesName}[] = {")
        foreach ($frame in $frameResults) {
            $hash = Get-Fnv1a32 -Value $frame.SceneId
            [void]$builder.AppendLine(('    {{ 0x{0:X8}U, {1}, sizeof({1}) }},' -f
                                       $hash, $frame.ArrayName))
        }
        [void]$builder.AppendLine('};')
        [void]$builder.AppendLine()
        $packResults += [pscustomobject]@{
            PackId = $packId
            DisplayName = [string]$pack.DisplayName
            PaletteName = $paletteName
            PaletteCount = $palette.Count
            FramesName = $framesName
            FrameCount = $frameResults.Count
        }
    }

    [void]$builder.AppendLine('static const companion_expression_scene_t s_scenes[] = {')
    foreach ($scene in $scenes) {
        $sceneHash = Get-Fnv1a32 -Value ([string]$scene.SceneId)
        $fallbackHash = if ($null -eq $scene.Fallback) {
            [uint32]0
        } else {
            Get-Fnv1a32 -Value ([string]$scene.Fallback)
        }
        $required = if ([bool]$scene.Required) { 'true' } else { 'false' }
        [void]$builder.AppendLine(('    {{ 0x{0:X8}U, "{1}", {2}, 0x{3:X8}U }},' -f
                                   $sceneHash, $scene.SceneId, $required,
                                   $fallbackHash))
    }
    [void]$builder.AppendLine('};')
    [void]$builder.AppendLine()
    [void]$builder.AppendLine('static const companion_expression_pack_t s_packs[] = {')
    foreach ($pack in $packResults) {
        $packHash = Get-Fnv1a32 -Value $pack.PackId
        [void]$builder.AppendLine(('    {{ 0x{0:X8}U, "{1}", "{2}", {3}, {4}U, {5}, {6}U }},' -f
                                   $packHash, $pack.PackId, $pack.DisplayName,
                                   $pack.PaletteName, $pack.PaletteCount,
                                   $pack.FramesName, $pack.FrameCount))
    }
    [void]$builder.AppendLine('};')
    [void]$builder.AppendLine()
    $defaultHash = Get-Fnv1a32 -Value ([string]$manifest.DefaultPackId)
    [void]$builder.AppendLine('const companion_expression_catalog_t g_companion_expression_catalog = {')
    [void]$builder.AppendLine('    .packs = s_packs,')
    [void]$builder.AppendLine('    .pack_count = sizeof(s_packs) / sizeof(s_packs[0]),')
    [void]$builder.AppendLine('    .scenes = s_scenes,')
    [void]$builder.AppendLine('    .scene_count = sizeof(s_scenes) / sizeof(s_scenes[0]),')
    [void]$builder.AppendLine(('    .default_pack_id = 0x{0:X8}U,' -f $defaultHash))
    [void]$builder.AppendLine('};')

    [IO.File]::WriteAllText($TempSource, $builder.ToString(),
                            [Text.UTF8Encoding]::new($false))

    $backupPacks = Join-Path $PSScriptRoot ('.packs_backup_' + [guid]::NewGuid().ToString('N'))
    if (Test-Path -LiteralPath $PackOutputRoot) {
        Move-Item -LiteralPath $PackOutputRoot -Destination $backupPacks
    }
    try {
        Move-Item -LiteralPath $TempPacks -Destination $PackOutputRoot
        Move-Item -LiteralPath $TempSource -Destination $GeneratedSource -Force
        if (Test-Path -LiteralPath $backupPacks) {
            Remove-Item -LiteralPath $backupPacks -Recurse -Force
        }
    }
    catch {
        if ((-not (Test-Path -LiteralPath $PackOutputRoot)) -and
            (Test-Path -LiteralPath $backupPacks)) {
            Move-Item -LiteralPath $backupPacks -Destination $PackOutputRoot
        }
        throw
    }

    Write-Output ("Generated {0} packs x {1} scenes ({2} frames)" -f
                  $packs.Count, $scenes.Count,
                  ($packs.Count * $scenes.Count))
}
finally {
    if (Test-Path -LiteralPath $TempRoot) {
        Remove-Item -LiteralPath $TempRoot -Recurse -Force
    }
}
