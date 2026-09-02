$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$logical_width = 80
$logical_height = 60
$preview_scale = 12
$preview_width = $logical_width * $preview_scale
$preview_height = $logical_height * $preview_scale
$background = '#05070A'
$concepts_root = Join-Path $PSScriptRoot 'concepts'

$themes = @(
    [pscustomobject]@{ Pack = 'crimson_slime'; Shape = 'slime'; Outline = '#6E2B36'; Body = '#B84A5F'; Accent = '#D66A7C'; Highlight = '#E8C7A8'; Eye = '#F2E7DF' },
    [pscustomobject]@{ Pack = 'jade_frog'; Shape = 'frog'; Outline = '#245344'; Body = '#3E8068'; Accent = '#69A98E'; Highlight = '#C1DCCA'; Eye = '#E2EEE5' },
    [pscustomobject]@{ Pack = 'bone_skull'; Shape = 'skull'; Outline = '#3E424A'; Body = '#B9B3A6'; Accent = '#8B8390'; Highlight = '#F0EBDD'; Eye = '#E7E0D4' },
    [pscustomobject]@{ Pack = 'cobalt_owl'; Shape = 'owl'; Outline = '#2A405F'; Body = '#456A99'; Accent = '#7295BE'; Highlight = '#C8D8E8'; Eye = '#E7EEF4' },
    [pscustomobject]@{ Pack = 'magenta_octopus'; Shape = 'octopus'; Outline = '#603652'; Body = '#985978'; Accent = '#C17B9D'; Highlight = '#E4C4D4'; Eye = '#F0E4EA' },
    [pscustomobject]@{ Pack = 'silver_husky'; Shape = 'husky'; Outline = '#28333D'; Body = '#788896'; Accent = '#4C5F70'; Highlight = '#D6E0E4'; Eye = '#78D5DF' },
    [pscustomobject]@{ Pack = 'amber_duck'; Shape = 'duck'; Outline = '#244F68'; Body = '#F4D536'; Accent = '#F2648C'; Highlight = '#FFE992'; Eye = '#F8FCFF' }
)

function New-BrushMap {
    param([pscustomobject]$Theme)

    $colors = @{
        Background = $background
        Outline = $Theme.Outline
        Body = $Theme.Body
        Accent = $Theme.Accent
        Highlight = $Theme.Highlight
        Eye = $Theme.Eye
        Dark = '#0B1220'
    }
    $brushes = @{}
    foreach ($name in $colors.Keys) {
        $brushes[$name] = [System.Drawing.SolidBrush]::new(
            [System.Drawing.ColorTranslator]::FromHtml($colors[$name])
        )
    }
    return $brushes
}

function Add-PixelRect {
    param(
        [System.Drawing.Graphics]$Graphics,
        [hashtable]$Brushes,
        [int]$Scale,
        [int]$X,
        [int]$Y,
        [int]$Width,
        [int]$Height,
        [string]$Brush
    )

    $Graphics.FillRectangle(
        $Brushes[$Brush],
        $X * $Scale,
        $Y * $Scale,
        $Width * $Scale,
        $Height * $Scale
    )
}

function Add-PixelPolygon {
    param(
        [System.Drawing.Graphics]$Graphics,
        [hashtable]$Brushes,
        [int]$Scale,
        [int[][]]$Coordinates,
        [string]$Brush
    )

    $points = [System.Drawing.Point[]]@(
        foreach ($coordinate in $Coordinates) {
            [System.Drawing.Point]::new($coordinate[0] * $Scale, $coordinate[1] * $Scale)
        }
    )
    $Graphics.FillPolygon($Brushes[$Brush], $points)
}

function Add-FrameMarks {
    param(
        [System.Drawing.Graphics]$Graphics,
        [hashtable]$Brushes,
        [int]$Scale
    )

    Add-PixelRect $Graphics $Brushes $Scale 11 12 3 1 'Outline'
    Add-PixelRect $Graphics $Brushes $Scale 66 12 3 1 'Outline'
}

function Draw-Slime {
    param([System.Drawing.Graphics]$Graphics, [hashtable]$Brushes, [int]$Scale)

    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(34, 18), @(46, 18), @(46, 20), @(51, 20), @(51, 23), @(55, 23),
        @(55, 27), @(58, 27), @(58, 39), @(56, 39), @(56, 43), @(52, 43),
        @(52, 46), @(28, 46), @(28, 44), @(24, 44), @(24, 41), @(22, 41),
        @(22, 31), @(24, 31), @(24, 27), @(27, 27), @(27, 23), @(31, 23),
        @(31, 20), @(34, 20)
    ) 'Outline'
    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(35, 20), @(45, 20), @(45, 22), @(49, 22), @(49, 25), @(53, 25),
        @(53, 29), @(56, 29), @(56, 37), @(54, 37), @(54, 41), @(50, 41),
        @(50, 44), @(30, 44), @(30, 42), @(26, 42), @(26, 39), @(24, 39),
        @(24, 32), @(26, 32), @(26, 29), @(29, 29), @(29, 25), @(33, 25),
        @(33, 22), @(35, 22)
    ) 'Body'
    Add-PixelRect $Graphics $Brushes $Scale 30 30 3 4 'Eye'
    Add-PixelRect $Graphics $Brushes $Scale 47 29 3 4 'Highlight'
    Add-PixelRect $Graphics $Brushes $Scale 38 38 5 1 'Dark'
    Add-PixelRect $Graphics $Brushes $Scale 31 23 5 2 'Accent'
    Add-PixelRect $Graphics $Brushes $Scale 26 29 2 6 'Accent'
}

function Draw-Frog {
    param([System.Drawing.Graphics]$Graphics, [hashtable]$Brushes, [int]$Scale)

    Add-PixelRect $Graphics $Brushes $Scale 23 17 10 11 'Outline'
    Add-PixelRect $Graphics $Brushes $Scale 48 18 9 10 'Outline'
    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(20, 45), @(20, 33), @(22, 33), @(22, 29), @(24, 29), @(24, 25),
        @(57, 25), @(57, 29), @(59, 29), @(59, 33), @(61, 33), @(61, 45),
        @(59, 45), @(59, 48), @(56, 48), @(56, 50), @(25, 50), @(25, 48),
        @(22, 48), @(22, 45)
    ) 'Outline'
    Add-PixelRect $Graphics $Brushes $Scale 25 19 6 8 'Body'
    Add-PixelRect $Graphics $Brushes $Scale 50 20 5 7 'Body'
    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(22, 43), @(22, 34), @(24, 34), @(24, 31), @(26, 31), @(26, 27),
        @(55, 27), @(55, 31), @(57, 31), @(57, 34), @(59, 34), @(59, 43),
        @(57, 43), @(57, 46), @(54, 46), @(54, 48), @(27, 48), @(27, 46),
        @(24, 46), @(24, 43)
    ) 'Body'
    Add-PixelRect $Graphics $Brushes $Scale 26 22 3 4 'Eye'
    Add-PixelRect $Graphics $Brushes $Scale 51 23 3 3 'Eye'
    Add-PixelRect $Graphics $Brushes $Scale 35 42 11 1 'Highlight'
    Add-PixelRect $Graphics $Brushes $Scale 24 36 3 2 'Accent'
    Add-PixelRect $Graphics $Brushes $Scale 54 36 3 2 'Accent'
}

function Draw-Skull {
    param([System.Drawing.Graphics]$Graphics, [hashtable]$Brushes, [int]$Scale)

    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(28, 19), @(52, 19), @(52, 22), @(56, 22), @(56, 26), @(59, 26),
        @(59, 38), @(56, 38), @(56, 42), @(51, 42), @(51, 47), @(47, 47),
        @(47, 49), @(33, 49), @(33, 47), @(29, 47), @(29, 42), @(24, 42),
        @(24, 38), @(21, 38), @(21, 26), @(24, 26), @(24, 22), @(28, 22)
    ) 'Outline'
    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(30, 21), @(50, 21), @(50, 24), @(54, 24), @(54, 28), @(57, 28),
        @(57, 36), @(54, 36), @(54, 40), @(49, 40), @(49, 45), @(46, 45),
        @(46, 47), @(34, 47), @(34, 45), @(31, 45), @(31, 40), @(26, 40),
        @(26, 36), @(23, 36), @(23, 28), @(26, 28), @(26, 24), @(30, 24)
    ) 'Body'
    Add-PixelRect $Graphics $Brushes $Scale 28 29 8 8 'Dark'
    Add-PixelRect $Graphics $Brushes $Scale 45 28 8 8 'Dark'
    Add-PixelRect $Graphics $Brushes $Scale 38 36 4 4 'Dark'
    Add-PixelRect $Graphics $Brushes $Scale 35 43 11 2 'Highlight'
    Add-PixelRect $Graphics $Brushes $Scale 38 43 1 4 'Outline'
    Add-PixelRect $Graphics $Brushes $Scale 43 43 1 4 'Outline'
    Add-PixelRect $Graphics $Brushes $Scale 28 24 6 2 'Highlight'
    Add-PixelRect $Graphics $Brushes $Scale 50 25 3 2 'Accent'
}

function Draw-Owl {
    param([System.Drawing.Graphics]$Graphics, [hashtable]$Brushes, [int]$Scale)

    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(24, 15), @(30, 15), @(30, 19), @(50, 19), @(50, 14), @(56, 14),
        @(56, 21), @(58, 21), @(58, 26), @(60, 26), @(60, 42), @(58, 42),
        @(58, 45), @(55, 45), @(55, 48), @(51, 48), @(51, 50), @(44, 50),
        @(44, 52), @(36, 52), @(36, 50), @(29, 50), @(29, 48), @(25, 48),
        @(25, 46), @(22, 46), @(22, 44), @(20, 44), @(20, 28), @(22, 28),
        @(22, 22), @(24, 22)
    ) 'Outline'
    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(26, 18), @(29, 18), @(29, 22), @(51, 22), @(51, 17), @(54, 17),
        @(54, 23), @(56, 23), @(56, 28), @(58, 28), @(58, 40), @(56, 40),
        @(56, 43), @(53, 43), @(53, 46), @(49, 46), @(49, 48), @(43, 48),
        @(43, 50), @(37, 50), @(37, 48), @(31, 48), @(31, 46), @(27, 46),
        @(27, 44), @(24, 44), @(24, 42), @(22, 42), @(22, 30), @(24, 30),
        @(24, 25), @(26, 25)
    ) 'Body'
    Add-PixelRect $Graphics $Brushes $Scale 25 27 11 10 'Accent'
    Add-PixelRect $Graphics $Brushes $Scale 45 27 11 10 'Accent'
    Add-PixelRect $Graphics $Brushes $Scale 28 30 4 5 'Eye'
    Add-PixelRect $Graphics $Brushes $Scale 49 30 4 5 'Eye'
    Add-PixelRect $Graphics $Brushes $Scale 30 32 1 2 'Dark'
    Add-PixelRect $Graphics $Brushes $Scale 51 32 1 2 'Dark'
    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(40, 35), @(43, 39), @(40, 43), @(37, 39)
    ) 'Highlight'
    Add-PixelRect $Graphics $Brushes $Scale 22 37 3 5 'Accent'
    Add-PixelRect $Graphics $Brushes $Scale 55 36 3 5 'Accent'
}

function Draw-Octopus {
    param([System.Drawing.Graphics]$Graphics, [hashtable]$Brushes, [int]$Scale)

    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(26, 40), @(26, 30), @(29, 30), @(29, 25), @(34, 25), @(34, 22),
        @(47, 22), @(47, 24), @(52, 24), @(52, 28), @(55, 28), @(55, 39),
        @(62, 39), @(62, 43), @(59, 43), @(59, 47), @(54, 47), @(54, 43),
        @(52, 43), @(52, 52), @(47, 52), @(47, 44), @(44, 44), @(44, 50),
        @(39, 50), @(39, 44), @(36, 44), @(36, 52), @(31, 52), @(31, 43),
        @(29, 43), @(29, 49), @(23, 49), @(23, 47), @(20, 47), @(20, 41),
        @(26, 41)
    ) 'Outline'
    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(28, 38), @(28, 31), @(31, 31), @(31, 27), @(36, 27), @(36, 24),
        @(45, 24), @(45, 26), @(50, 26), @(50, 30), @(53, 30), @(53, 41),
        @(60, 41), @(60, 42), @(57, 42), @(57, 45), @(56, 45), @(56, 41),
        @(50, 41), @(50, 50), @(49, 50), @(49, 42), @(42, 42), @(42, 48),
        @(41, 48), @(41, 42), @(34, 42), @(34, 50), @(33, 50), @(33, 41),
        @(27, 41), @(27, 47), @(25, 47), @(25, 45), @(22, 45), @(22, 43),
        @(27, 43), @(27, 40), @(28, 40)
    ) 'Body'
    Add-PixelRect $Graphics $Brushes $Scale 33 31 3 4 'Eye'
    Add-PixelRect $Graphics $Brushes $Scale 47 30 3 4 'Eye'
    Add-PixelRect $Graphics $Brushes $Scale 39 37 4 1 'Highlight'
    Add-PixelRect $Graphics $Brushes $Scale 33 27 6 2 'Accent'
    Add-PixelRect $Graphics $Brushes $Scale 30 30 2 5 'Accent'
}

function Draw-Husky {
    param([System.Drawing.Graphics]$Graphics, [hashtable]$Brushes, [int]$Scale)

    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(25, 25), @(25, 14), @(30, 14), @(30, 18), @(34, 18), @(34, 22),
        @(46, 22), @(46, 18), @(50, 18), @(50, 14), @(55, 14), @(55, 26),
        @(59, 26), @(59, 30), @(61, 30), @(61, 42), @(58, 42), @(58, 46),
        @(52, 46), @(52, 49), @(28, 49), @(28, 47), @(22, 47), @(22, 44),
        @(19, 44), @(19, 30), @(21, 30), @(21, 25)
    ) 'Outline'
    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(27, 25), @(27, 17), @(29, 17), @(29, 20), @(33, 20), @(33, 24),
        @(47, 24), @(47, 20), @(51, 20), @(51, 17), @(53, 17), @(53, 28),
        @(57, 28), @(57, 32), @(59, 32), @(59, 40), @(56, 40), @(56, 44),
        @(50, 44), @(50, 47), @(30, 47), @(30, 45), @(24, 45), @(24, 42),
        @(21, 42), @(21, 32), @(23, 32), @(23, 27), @(27, 27)
    ) 'Body'
    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(26, 18), @(28, 18), @(28, 23), @(25, 23)
    ) 'Accent'
    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(52, 18), @(54, 18), @(55, 24), @(52, 23)
    ) 'Accent'
    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(24, 26), @(31, 26), @(31, 29), @(35, 29), @(35, 38), @(31, 38),
        @(31, 41), @(27, 41), @(27, 37), @(24, 37)
    ) 'Highlight'
    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(49, 26), @(56, 26), @(56, 37), @(53, 37), @(53, 41), @(49, 41),
        @(49, 38), @(45, 38), @(45, 29), @(49, 29)
    ) 'Highlight'
    Add-PixelRect $Graphics $Brushes $Scale 30 31 4 3 'Eye'
    Add-PixelRect $Graphics $Brushes $Scale 47 31 4 3 'Eye'
    Add-PixelRect $Graphics $Brushes $Scale 32 32 1 2 'Dark'
    Add-PixelRect $Graphics $Brushes $Scale 48 32 1 2 'Dark'
    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(33, 38), @(47, 38), @(47, 40), @(52, 40), @(52, 45), @(47, 45),
        @(47, 47), @(33, 47), @(33, 45), @(28, 45), @(28, 40), @(33, 40)
    ) 'Highlight'
    Add-PixelRect $Graphics $Brushes $Scale 38 39 5 3 'Dark'
    Add-PixelRect $Graphics $Brushes $Scale 40 42 2 4 'Dark'
    Add-PixelRect $Graphics $Brushes $Scale 24 34 3 2 'Accent'
    Add-PixelRect $Graphics $Brushes $Scale 54 34 3 2 'Accent'
}

function Draw-Duck {
    param([System.Drawing.Graphics]$Graphics, [hashtable]$Brushes, [int]$Scale)

    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(33, 21), @(33, 18), @(37, 18), @(37, 16), @(41, 16), @(41, 19),
        @(47, 19), @(47, 21), @(52, 21), @(52, 23), @(57, 23), @(57, 26),
        @(60, 26), @(60, 30), @(62, 30), @(62, 42), @(60, 42), @(60, 45),
        @(56, 45), @(56, 48), @(51, 48), @(51, 50), @(29, 50), @(29, 48),
        @(24, 48), @(24, 46), @(20, 46), @(20, 43), @(18, 43), @(18, 31),
        @(20, 31), @(20, 27), @(24, 27), @(24, 24), @(29, 24), @(29, 21)
    ) 'Outline'
    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(34, 23), @(34, 20), @(39, 20), @(39, 18), @(40, 18), @(40, 21),
        @(46, 21), @(46, 23), @(51, 23), @(51, 25), @(55, 25), @(55, 28),
        @(58, 28), @(58, 32), @(60, 32), @(60, 40), @(58, 40), @(58, 43),
        @(54, 43), @(54, 46), @(49, 46), @(49, 48), @(31, 48), @(31, 46),
        @(26, 46), @(26, 44), @(22, 44), @(22, 41), @(20, 41), @(20, 33),
        @(22, 33), @(22, 29), @(26, 29), @(26, 26), @(31, 26), @(31, 23)
    ) 'Body'

    Add-PixelRect $Graphics $Brushes $Scale 27 29 6 7 'Eye'
    Add-PixelRect $Graphics $Brushes $Scale 48 28 6 7 'Eye'
    Add-PixelRect $Graphics $Brushes $Scale 30 31 2 3 'Dark'
    Add-PixelRect $Graphics $Brushes $Scale 49 30 2 3 'Dark'

    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(31, 37), @(35, 37), @(35, 35), @(46, 35), @(46, 37), @(51, 37),
        @(51, 42), @(47, 42), @(47, 44), @(35, 44), @(35, 42), @(31, 42)
    ) 'Outline'
    Add-PixelPolygon $Graphics $Brushes $Scale @(
        @(33, 38), @(37, 38), @(37, 37), @(44, 37), @(44, 39), @(49, 39),
        @(49, 41), @(45, 41), @(45, 42), @(37, 42), @(37, 41), @(33, 41)
    ) 'Accent'

    Add-PixelRect $Graphics $Brushes $Scale 26 25 7 2 'Highlight'
    Add-PixelRect $Graphics $Brushes $Scale 22 37 3 3 'Accent'
    Add-PixelRect $Graphics $Brushes $Scale 56 36 3 3 'Highlight'
}

function Render-Theme {
    param([pscustomobject]$Theme)

    $brushes = New-BrushMap $Theme
    $bitmap = [System.Drawing.Bitmap]::new(
        $preview_width,
        $preview_height,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
    )
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $graphics.Clear([System.Drawing.ColorTranslator]::FromHtml($background))

    try {
        Add-FrameMarks $graphics $brushes $preview_scale
        switch ($Theme.Shape) {
            'slime' { Draw-Slime $graphics $brushes $preview_scale }
            'frog' { Draw-Frog $graphics $brushes $preview_scale }
            'skull' { Draw-Skull $graphics $brushes $preview_scale }
            'owl' { Draw-Owl $graphics $brushes $preview_scale }
            'octopus' { Draw-Octopus $graphics $brushes $preview_scale }
            'husky' { Draw-Husky $graphics $brushes $preview_scale }
            'duck' { Draw-Duck $graphics $brushes $preview_scale }
            default { throw "Unknown theme shape: $($Theme.Shape)" }
        }

        $output_dir = Join-Path $concepts_root $Theme.Pack
        [System.IO.Directory]::CreateDirectory($output_dir) | Out-Null
        $output_path = Join-Path $output_dir ($Theme.Pack + '_idle_preview_3x.png')
        $bitmap.Save($output_path, [System.Drawing.Imaging.ImageFormat]::Png)
        return $output_path
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
        foreach ($brush in $brushes.Values) {
            $brush.Dispose()
        }
    }
}

function New-Overview {
    param([string[]]$ImagePaths)

    $cell_width = 640
    $cell_height = 480
    $overview = [System.Drawing.Bitmap]::new(
        $cell_width * 4,
        $cell_height * 2,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
    )
    $graphics = [System.Drawing.Graphics]::FromImage($overview)
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $graphics.Clear([System.Drawing.ColorTranslator]::FromHtml($background))

    try {
        for ($index = 0; $index -lt $ImagePaths.Count; $index++) {
            $image = [System.Drawing.Image]::FromFile($ImagePaths[$index])
            try {
                $x = ($index % 4) * $cell_width
                $y = [math]::Floor($index / 4) * $cell_height
                $graphics.DrawImage($image, $x, $y, $cell_width, $cell_height)
            }
            finally {
                $image.Dispose()
            }
        }

        $overview_path = Join-Path $concepts_root 'theme_baselines_overview_2x.png'
        $overview.Save($overview_path, [System.Drawing.Imaging.ImageFormat]::Png)
        return $overview_path
    }
    finally {
        $graphics.Dispose()
        $overview.Dispose()
    }
}

$rendered_paths = @(
    Join-Path $concepts_root 'icebox\icebox_idle_preview_3x.png'
)
foreach ($theme in $themes) {
    $rendered_paths += Render-Theme $theme
}

New-Overview $rendered_paths
