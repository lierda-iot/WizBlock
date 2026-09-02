param()

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$LogicalWidth = 80
$LogicalHeight = 60
$SurfaceWidth = 320
$SurfaceHeight = 240
$ScalePercent = 110
$OffsetX = 0
$OffsetY = -8
$TopSafeHeight = 32
$NetworkSafeX = 220
$NetworkSafeY = 216
$PackRoot = Join-Path $PSScriptRoot '..\assets\expressions\packs'

$framePaths = @(Get-ChildItem -LiteralPath $PackRoot -Recurse -Filter '*_80x60.png')
if (96 -ne $framePaths.Count) {
    throw "Expected 96 production frames, found $($framePaths.Count)"
}

$targetWidth = [int](($SurfaceWidth * $ScalePercent) / 100)
$targetHeight = [int](($SurfaceHeight * $ScalePercent) / 100)
$targetX = [int](($SurfaceWidth - $targetWidth) / 2) + $OffsetX
$targetY = [int](($SurfaceHeight - $targetHeight) / 2) + $OffsetY

foreach ($framePath in $framePaths) {
    $bitmap = [Drawing.Bitmap]::new($framePath.FullName)
    try {
        if ($LogicalWidth -ne $bitmap.Width -or
            $LogicalHeight -ne $bitmap.Height) {
            throw "Unexpected frame dimensions: $($framePath.FullName)"
        }

        $background = $bitmap.GetPixel(0, 0).ToArgb()
        foreach ($markX in @(11, 12, 13, 66, 67, 68)) {
            if ($background -ne $bitmap.GetPixel($markX, 12).ToArgb()) {
                throw "Theme mark remains in $($framePath.Name) at ($markX,12)"
            }
        }

        $oldMinY = $SurfaceHeight
        $oldMinX = $SurfaceWidth
        $oldMaxX = -1
        for ($sourceY = 0; $sourceY -lt $LogicalHeight; $sourceY++) {
            for ($sourceX = 0; $sourceX -lt $LogicalWidth; $sourceX++) {
                if ($background -ne $bitmap.GetPixel($sourceX, $sourceY).ToArgb()) {
                    $oldMinY = [Math]::Min($oldMinY, $sourceY * 4)
                    $oldMinX = [Math]::Min($oldMinX, $sourceX * 4)
                    $oldMaxX = [Math]::Max($oldMaxX, ($sourceX + 1) * 4 - 1)
                }
            }
        }

        $newMinY = $SurfaceHeight
        $newMinX = $SurfaceWidth
        $newMaxX = -1
        for ($outputY = 0; $outputY -lt $SurfaceHeight; $outputY++) {
            $relativeY = $outputY - $targetY
            if ($relativeY -lt 0 -or $relativeY -ge $targetHeight) {
                continue
            }
            $sourceY = [int](($relativeY * $LogicalHeight) / $targetHeight)
            for ($outputX = 0; $outputX -lt $SurfaceWidth; $outputX++) {
                $relativeX = $outputX - $targetX
                if ($relativeX -lt 0 -or $relativeX -ge $targetWidth) {
                    continue
                }
                $sourceX = [int](($relativeX * $LogicalWidth) / $targetWidth)
                if ($background -eq $bitmap.GetPixel($sourceX, $sourceY).ToArgb()) {
                    continue
                }
                if ($outputY -lt $TopSafeHeight -or
                    ($outputX -ge $NetworkSafeX -and $outputY -ge $NetworkSafeY)) {
                    throw "Subject enters status safe area in $($framePath.Name) at ($outputX,$outputY)"
                }
                $newMinY = [Math]::Min($newMinY, $outputY)
                $newMinX = [Math]::Min($newMinX, $outputX)
                $newMaxX = [Math]::Max($newMaxX, $outputX)
            }
        }

        if ($newMinY -ge $oldMinY) {
            throw "Subject did not move upward in $($framePath.Name): old=$oldMinY new=$newMinY"
        }
        if (($newMaxX - $newMinX) -le ($oldMaxX - $oldMinX)) {
            throw "Subject did not grow in $($framePath.Name)"
        }
    }
    finally {
        $bitmap.Dispose()
    }
}

Write-Host ('PASS expression assets: 96 frames, marks removed, ' +
    '110% scale, -8px Y offset, status safe areas clear')
