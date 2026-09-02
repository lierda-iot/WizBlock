param(
    [string]$OutputPath = (Join-Path $PSScriptRoot 'concepts\icebox\icebox_idle_preview_3x.png')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$logical_width = 80
$logical_height = 60
$scale = 12
$canvas_width = $logical_width * $scale
$canvas_height = $logical_height * $scale

$palette = @{
    Background = '#05070A'
    Shadow = '#081218'
    Outline = '#164E63'
    Edge = '#22D3EE'
    Face = '#0B1F29'
    FaceLight = '#103541'
    Accent = '#5EEAD4'
    Ice = '#67E8F9'
    White = '#F8FAFC'
    Muted = '#94A3B8'
    Pupil = '#071116'
}

$brushes = @{}
foreach ($name in $palette.Keys) {
    $color = [System.Drawing.ColorTranslator]::FromHtml($palette[$name])
    $brushes[$name] = [System.Drawing.SolidBrush]::new($color)
}

$bitmap = [System.Drawing.Bitmap]::new(
    $canvas_width,
    $canvas_height,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
)
$graphics = [System.Drawing.Graphics]::FromImage($bitmap)
$graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None
$graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$graphics.Clear([System.Drawing.ColorTranslator]::FromHtml($palette.Background))

function Add-PixelRect {
    param(
        [int]$X,
        [int]$Y,
        [int]$Width,
        [int]$Height,
        [string]$Brush
    )

    $graphics.FillRectangle(
        $brushes[$Brush],
        $X * $scale,
        $Y * $scale,
        $Width * $scale,
        $Height * $scale
    )
}

function Add-PixelPolygon {
    param(
        [int[][]]$Coordinates,
        [string]$Brush
    )

    $points = [System.Drawing.Point[]]@(
        foreach ($coordinate in $Coordinates) {
            [System.Drawing.Point]::new($coordinate[0] * $scale, $coordinate[1] * $scale)
        }
    )
    $graphics.FillPolygon($brushes[$Brush], $points)
}

try {
    # Small, blocky silhouette leaves most of the 320x240 screen as quiet space.
    Add-PixelPolygon @(
        @(22, 19), @(27, 19), @(30, 22), @(50, 22), @(53, 19),
        @(58, 19), @(58, 24), @(60, 27), @(60, 43), @(57, 43),
        @(57, 46), @(23, 46), @(23, 44), @(20, 44), @(20, 27), @(22, 24)
    ) 'Outline'
    Add-PixelPolygon @(
        @(23, 21), @(27, 21), @(30, 24), @(50, 24), @(53, 21),
        @(57, 21), @(57, 26), @(58, 28), @(58, 42), @(55, 42),
        @(55, 44), @(25, 44), @(25, 42), @(22, 42), @(22, 28), @(23, 26)
    ) 'Face'

    # Two modest ear insets add identity without competing with the face.
    Add-PixelRect 23 22 3 3 'FaceLight'
    Add-PixelRect 24 22 1 2 'Accent'
    Add-PixelRect 54 22 3 3 'FaceLight'
    Add-PixelRect 55 22 1 2 'Ice'
    Add-PixelRect 30 24 20 1 'FaceLight'

    # Deliberately minimal facial features: uneven square eyes and a short mouth line.
    Add-PixelRect 29 31 4 5 'Ice'
    Add-PixelRect 47 30 4 4 'Accent'
    Add-PixelRect 38 40 5 1 'Muted'

    # Small cool cheek marks provide depth while retaining generous empty space.
    Add-PixelRect 25 38 3 1 'FaceLight'
    Add-PixelRect 52 38 3 1 'FaceLight'
    Add-PixelRect 11 12 3 1 'FaceLight'
    Add-PixelRect 66 12 3 1 'FaceLight'

    $output_directory = Split-Path -Parent $OutputPath
    [System.IO.Directory]::CreateDirectory($output_directory) | Out-Null
    $bitmap.Save($OutputPath, [System.Drawing.Imaging.ImageFormat]::Png)
    (Resolve-Path -LiteralPath $OutputPath).Path
}
finally {
    $graphics.Dispose()
    $bitmap.Dispose()
    foreach ($brush in $brushes.Values) {
        $brush.Dispose()
    }
}
