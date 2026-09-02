$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$logical_width = 80
$logical_height = 60
$preview_scale = 12
$preview_width = $logical_width * $preview_scale
$preview_height = $logical_height * $preview_scale
$overview_scale = 4
$background = '#05070A'
$concepts_root = Join-Path $PSScriptRoot 'concepts'

$states = @(
    'idle',
    'blink',
    'idle_sway_left_up',
    'idle_sway_right_up',
    'listen_focus',
    'think',
    'turn_gaze_left',
    'turn_gaze_right',
    'talk_closed',
    'talk_open',
    'touch_pout_compress',
    'touch_pout_expand'
)

$themes = @(
    [pscustomobject]@{
        Pack = 'icebox'; Source = 'icebox\icebox_idle_preview_3x.png'
        Outline = '#164E63'; Body = '#0B1F29'; Accent = '#5EEAD4'; Highlight = '#103541'; Eye = '#67E8F9'; Dark = '#071116'
        Cover = '#0B1F29'; MouthCover = '#0B1F29'; Cheek = '#103541'; Mark = '#103541'; Pupil = '#071116'; Blink = '#071116'
        MouthEdge = '#94A3B8'; MouthInner = '#164E63'; MouthAccent = '#5EEAD4'
        EyeLeft = @(29, 31, 4, 5); EyeRight = @(47, 30, 4, 4); Mouth = @(38, 40, 5, 1); CheekLeft = @(25, 38); CheekRight = @(52, 38)
    },
    [pscustomobject]@{
        Pack = 'crimson_slime'; Source = 'crimson_slime\crimson_slime_idle_preview_3x.png'
        Outline = '#6E2B36'; Body = '#B84A5F'; Accent = '#D66A7C'; Highlight = '#E8C7A8'; Eye = '#F2E7DF'; Dark = '#0B1220'
        Cover = '#B84A5F'; MouthCover = '#B84A5F'; Cheek = '#D66A7C'; Mark = '#6E2B36'; Pupil = '#0B1220'; Blink = '#0B1220'
        MouthEdge = '#6E2B36'; MouthInner = '#91384B'; MouthAccent = '#D66A7C'
        EyeLeft = @(30, 30, 3, 4); EyeRight = @(47, 29, 3, 4); Mouth = @(38, 38, 5, 1); CheekLeft = @(27, 36); CheekRight = @(50, 35)
    },
    [pscustomobject]@{
        Pack = 'jade_frog'; Source = 'jade_frog\jade_frog_idle_preview_3x.png'
        Outline = '#245344'; Body = '#3E8068'; Accent = '#69A98E'; Highlight = '#C1DCCA'; Eye = '#E2EEE5'; Dark = '#0B1220'
        Cover = '#3E8068'; MouthCover = '#3E8068'; Cheek = '#69A98E'; Mark = '#245344'; Pupil = '#0B1220'; Blink = '#0B1220'
        MouthEdge = '#C1DCCA'; MouthInner = '#2F6654'; MouthAccent = '#69A98E'
        EyeLeft = @(26, 22, 3, 4); EyeRight = @(51, 23, 3, 3); Mouth = @(35, 42, 11, 1); CheekLeft = @(24, 36); CheekRight = @(54, 36)
    },
    [pscustomobject]@{
        Pack = 'bone_skull'; Source = 'bone_skull\bone_skull_idle_preview_3x.png'
        Outline = '#3E424A'; Body = '#B9B3A6'; Accent = '#8B8390'; Highlight = '#F0EBDD'; Eye = '#E7E0D4'; Dark = '#0B1220'
        Cover = '#B9B3A6'; MouthCover = '#B9B3A6'; Cheek = '#8B8390'; Mark = '#3E424A'; Pupil = '#E7E0D4'; Blink = '#0B1220'
        MouthEdge = '#0B1220'; MouthInner = '#3E424A'; MouthAccent = '#F0EBDD'
        EyeLeft = @(28, 29, 8, 8); EyeRight = @(45, 28, 8, 8); Mouth = @(35, 43, 11, 4); CheekLeft = @(24, 37); CheekRight = @(54, 37)
    },
    [pscustomobject]@{
        Pack = 'cobalt_owl'; Source = 'cobalt_owl\cobalt_owl_idle_preview_3x.png'
        Outline = '#2A405F'; Body = '#456A99'; Accent = '#7295BE'; Highlight = '#C8D8E8'; Eye = '#E7EEF4'; Dark = '#0B1220'
        Cover = '#7295BE'; MouthCover = '#456A99'; Cheek = '#7295BE'; Mark = '#2A405F'; Pupil = '#0B1220'; Blink = '#0B1220'
        MouthEdge = '#C8D8E8'; MouthInner = '#365579'; MouthAccent = '#7295BE'
        EyeLeft = @(28, 30, 4, 5); EyeRight = @(49, 30, 4, 5); Mouth = @(37, 38, 7, 4); CheekLeft = @(23, 38); CheekRight = @(54, 37)
    },
    [pscustomobject]@{
        Pack = 'magenta_octopus'; Source = 'magenta_octopus\magenta_octopus_idle_preview_3x.png'
        Outline = '#603652'; Body = '#985978'; Accent = '#C17B9D'; Highlight = '#E4C4D4'; Eye = '#F0E4EA'; Dark = '#0B1220'
        Cover = '#985978'; MouthCover = '#985978'; Cheek = '#C17B9D'; Mark = '#603652'; Pupil = '#0B1220'; Blink = '#0B1220'
        MouthEdge = '#E4C4D4'; MouthInner = '#78435F'; MouthAccent = '#C17B9D'
        EyeLeft = @(33, 31, 3, 4); EyeRight = @(47, 30, 3, 4); Mouth = @(39, 37, 4, 1); CheekLeft = @(29, 36); CheekRight = @(51, 35)
    },
    [pscustomobject]@{
        Pack = 'silver_husky'; Source = 'silver_husky\silver_husky_idle_preview_3x.png'
        Outline = '#28333D'; Body = '#788896'; Accent = '#4C5F70'; Highlight = '#D6E0E4'; Eye = '#78D5DF'; Dark = '#0B1220'
        Cover = '#D6E0E4'; MouthCover = '#D6E0E4'; Cheek = '#4C5F70'; Mark = '#28333D'; Pupil = '#0B1220'; Blink = '#0B1220'
        MouthEdge = '#28333D'; MouthInner = '#4C5F70'; MouthAccent = '#78D5DF'
        EyeLeft = @(30, 31, 4, 3); EyeRight = @(47, 31, 4, 3); Mouth = @(38, 39, 5, 7); CheekLeft = @(24, 34); CheekRight = @(54, 34)
    },
    [pscustomobject]@{
        Pack = 'amber_duck'; Source = 'amber_duck\amber_duck_idle_preview_3x.png'
        Outline = '#244F68'; Body = '#F4D536'; Accent = '#F2648C'; Highlight = '#FFE992'; Eye = '#F8FCFF'; Dark = '#0B1220'
        Cover = '#F4D536'; MouthCover = '#F4D536'; Cheek = '#F2648C'; Mark = '#244F68'; Pupil = '#0B1220'; Blink = '#0B1220'
        MouthEdge = '#244F68'; MouthInner = '#A83E62'; MouthAccent = '#F2648C'
        EyeLeft = @(27, 29, 6, 7); EyeRight = @(48, 28, 6, 7); Mouth = @(33, 37, 16, 5); CheekLeft = @(22, 37); CheekRight = @(56, 36)
    }
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
        Dark = $Theme.Dark
        Cover = $Theme.Cover
        MouthCover = $Theme.MouthCover
        Cheek = $Theme.Cheek
        Mark = $Theme.Mark
        Pupil = $Theme.Pupil
        Blink = $Theme.Blink
        MouthEdge = $Theme.MouthEdge
        MouthInner = $Theme.MouthInner
        MouthAccent = $Theme.MouthAccent
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

function New-Frame {
    param(
        [System.Drawing.Image]$Source,
        [hashtable]$Brushes
    )

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
    $graphics.DrawImage($Source, 0, 0, $preview_width, $preview_height)
    Add-PixelRect $graphics $Brushes $preview_scale 11 12 3 1 'Background'
    Add-PixelRect $graphics $Brushes $preview_scale 66 12 3 1 'Background'

    return [pscustomobject]@{
        Bitmap = $bitmap
        Graphics = $graphics
    }
}

function Move-Subject {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.Image]$Source,
        [hashtable]$Brushes,
        [int]$OffsetX,
        [int]$OffsetY,
        [double]$ScaleX = 1.0
    )

    $source_rect = [System.Drawing.Rectangle]::new(16 * $preview_scale, 13 * $preview_scale, 48 * $preview_scale, 41 * $preview_scale)
    $clear_rect = [System.Drawing.Rectangle]::new(14 * $preview_scale, 11 * $preview_scale, 52 * $preview_scale, 44 * $preview_scale)
    $target_width = [int][math]::Round($source_rect.Width * $ScaleX)
    $target_x = [int][math]::Round((40 * $preview_scale) - ($target_width / 2) + ($OffsetX * $preview_scale))
    $target_y = $source_rect.Y + ($OffsetY * $preview_scale)
    $target_rect = [System.Drawing.Rectangle]::new($target_x, $target_y, $target_width, $source_rect.Height)

    $Graphics.FillRectangle($Brushes.Background, $clear_rect)
    $Graphics.DrawImage($Source, $target_rect, $source_rect, [System.Drawing.GraphicsUnit]::Pixel)
}

function Draw-Blink {
    param([System.Drawing.Graphics]$Graphics, [hashtable]$Brushes, [pscustomobject]$Theme)

    foreach ($eye in @($Theme.EyeLeft, $Theme.EyeRight)) {
        Add-PixelRect $Graphics $Brushes $preview_scale $eye[0] $eye[1] $eye[2] $eye[3] 'Cover'
        Add-PixelRect $Graphics $Brushes $preview_scale $eye[0] ($eye[1] + $eye[3] - 2) $eye[2] 1 'Blink'
    }
}

function Draw-DuckEyes {
    param(
        [System.Drawing.Graphics]$Graphics,
        [hashtable]$Brushes,
        [pscustomobject]$Theme,
        [ValidateSet('left', 'right', 'center', 'up_right')][string]$Direction
    )

    $pupil_offset_x = switch ($Direction) {
        'left' { 1 }
        'right' { 3 }
        'up_right' { 4 }
        default { 2 }
    }
    $pupil_offset_y = if ('up_right' -eq $Direction) { 1 } else { 2 }

    foreach ($eye in @($Theme.EyeLeft, $Theme.EyeRight)) {
        Add-PixelRect $Graphics $Brushes $preview_scale $eye[0] $eye[1] $eye[2] $eye[3] 'Eye'
        Add-PixelRect $Graphics $Brushes $preview_scale `
            ($eye[0] + $pupil_offset_x) ($eye[1] + $pupil_offset_y) 2 3 'Pupil'
    }
}

function Draw-Gaze {
    param(
        [System.Drawing.Graphics]$Graphics,
        [hashtable]$Brushes,
        [pscustomobject]$Theme,
        [ValidateSet('left', 'right', 'center')][string]$Direction
    )

    if ('amber_duck' -eq $Theme.Pack) {
        Draw-DuckEyes $Graphics $Brushes $Theme $Direction
        return
    }

    foreach ($eye in @($Theme.EyeLeft, $Theme.EyeRight)) {
        $pupil_width = [math]::Max(1, [math]::Min(2, $eye[2] - 1))
        $pupil_x = $eye[0] + [math]::Floor(($eye[2] - $pupil_width) / 2)
        if ('left' -eq $Direction) {
            $pupil_x = $eye[0]
        }
        elseif ('right' -eq $Direction) {
            $pupil_x = $eye[0] + $eye[2] - $pupil_width
        }

        $pupil_height = [math]::Max(1, [math]::Min(2, $eye[3] - 1))
        Add-PixelRect $Graphics $Brushes $preview_scale $pupil_x ($eye[1] + 1) $pupil_width $pupil_height 'Pupil'
    }
}

function Draw-Focus {
    param([System.Drawing.Graphics]$Graphics, [hashtable]$Brushes, [pscustomobject]$Theme)

    if ('amber_duck' -eq $Theme.Pack) {
        Draw-DuckEyes $Graphics $Brushes $Theme 'center'
        return
    }

    Draw-Gaze $Graphics $Brushes $Theme 'center'
    foreach ($eye in @($Theme.EyeLeft, $Theme.EyeRight)) {
        Add-PixelRect $Graphics $Brushes $preview_scale $eye[0] ($eye[1] - 2) $eye[2] 1 'Accent'
    }
}

function Draw-Think {
    param([System.Drawing.Graphics]$Graphics, [hashtable]$Brushes, [pscustomobject]$Theme)

    if ('amber_duck' -eq $Theme.Pack) {
        Draw-DuckEyes $Graphics $Brushes $Theme 'up_right'
        return
    }

    Draw-Gaze $Graphics $Brushes $Theme 'right'
    Add-PixelRect $Graphics $Brushes $preview_scale ($Theme.EyeRight[0] + 1) ($Theme.EyeRight[1] - 3) 2 1 'Highlight'
    Add-PixelRect $Graphics $Brushes $preview_scale ($Theme.EyeRight[0] + 2) ($Theme.EyeRight[1] - 2) 1 1 'Highlight'
}

function Draw-Talk {
    param(
        [System.Drawing.Graphics]$Graphics,
        [hashtable]$Brushes,
        [pscustomobject]$Theme,
        [bool]$Open
    )

    $mouth = $Theme.Mouth
    if ('bone_skull' -eq $Theme.Pack) {
        $clear_y = [math]::Max(0, $mouth[1] - 1)
        Add-PixelRect $Graphics $Brushes $preview_scale ($mouth[0] - 1) $clear_y ($mouth[2] + 2) ($mouth[3] + 3) 'MouthCover'
        if ($Open) {
            Add-PixelRect $Graphics $Brushes $preview_scale ($mouth[0] - 1) $clear_y ($mouth[2] + 2) 4 'Outline'
            Add-PixelRect $Graphics $Brushes $preview_scale $mouth[0] $mouth[1] $mouth[2] 2 'Dark'
        }
        else {
            Add-PixelRect $Graphics $Brushes $preview_scale $mouth[0] $mouth[1] $mouth[2] 1 'Dark'
        }
        return
    }

    if (-not $Open) {
        return
    }

    switch ($Theme.Pack) {
        'icebox' {
            Add-PixelRect $Graphics $Brushes $preview_scale 36 37 10 8 'MouthCover'
            Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                @(38, 38), @(43, 38), @(44, 39), @(44, 42),
                @(43, 43), @(38, 43), @(37, 42), @(37, 39)
            ) 'MouthEdge'
            Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                @(39, 39), @(42, 39), @(43, 40), @(43, 41),
                @(42, 42), @(39, 42), @(38, 41), @(38, 40)
            ) 'MouthInner'
            Add-PixelRect $Graphics $Brushes $preview_scale 40 42 2 1 'MouthAccent'
        }
        'crimson_slime' {
            Add-PixelRect $Graphics $Brushes $preview_scale 36 36 9 7 'MouthCover'
            Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                @(38, 37), @(43, 37), @(43, 38), @(45, 38), @(45, 41), @(43, 41),
                @(43, 42), @(38, 42), @(38, 41), @(36, 41), @(36, 38), @(38, 38)
            ) 'MouthEdge'
            Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                @(38, 38), @(43, 38), @(43, 40), @(42, 40), @(42, 41), @(39, 41), @(39, 40), @(38, 40)
            ) 'MouthInner'
            Add-PixelRect $Graphics $Brushes $preview_scale 40 40 2 1 'MouthAccent'
        }
        'jade_frog' {
            Add-PixelRect $Graphics $Brushes $preview_scale 33 39 15 8 'MouthCover'
            Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                @(35, 40), @(46, 40), @(46, 41), @(48, 41), @(48, 44), @(46, 44),
                @(46, 45), @(35, 45), @(35, 44), @(33, 44), @(33, 41), @(35, 41)
            ) 'MouthEdge'
            Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                @(35, 42), @(46, 42), @(46, 43), @(44, 43), @(44, 44), @(37, 44), @(37, 43), @(35, 43)
            ) 'MouthInner'
        }
        'cobalt_owl' {
            Add-PixelRect $Graphics $Brushes $preview_scale 35 33 11 14 'MouthCover'
            Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                @(40, 34), @(44, 39), @(40, 46), @(36, 39)
            ) 'MouthEdge'
            Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                @(40, 37), @(42, 40), @(40, 43), @(38, 40)
            ) 'MouthInner'
            Add-PixelRect $Graphics $Brushes $preview_scale 40 43 1 1 'MouthAccent'
        }
        'magenta_octopus' {
            Add-PixelRect $Graphics $Brushes $preview_scale 37 35 8 8 'MouthCover'
            Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                @(39, 36), @(43, 36), @(44, 37), @(44, 40), @(43, 41), @(39, 41), @(38, 40), @(38, 37)
            ) 'MouthEdge'
            Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                @(40, 37), @(42, 37), @(43, 38), @(43, 39), @(42, 40), @(40, 40), @(39, 39), @(39, 38)
            ) 'MouthInner'
        }
        'silver_husky' {
            Add-PixelRect $Graphics $Brushes $preview_scale 37 42 8 7 'MouthCover'
            Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                @(37, 42), @(45, 42), @(45, 44), @(44, 46),
                @(38, 46), @(37, 44)
            ) 'MouthEdge'
            Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                @(38, 43), @(44, 43), @(44, 44), @(43, 45),
                @(39, 45), @(38, 44)
            ) 'MouthInner'
            Add-PixelRect $Graphics $Brushes $preview_scale 40 45 1 1 'MouthAccent'
        }
        'amber_duck' {
            Add-PixelRect $Graphics $Brushes $preview_scale 30 33 22 16 'MouthCover'
            Add-PixelRect $Graphics $Brushes $preview_scale 31 36 20 10 'MouthEdge'
            Add-PixelRect $Graphics $Brushes $preview_scale 35 34 12 2 'MouthEdge'
            Add-PixelRect $Graphics $Brushes $preview_scale 35 46 12 2 'MouthEdge'
            Add-PixelRect $Graphics $Brushes $preview_scale 36 35 10 1 'MouthAccent'
            Add-PixelRect $Graphics $Brushes $preview_scale 32 36 18 4 'MouthAccent'
            Add-PixelRect $Graphics $Brushes $preview_scale 32 40 18 4 'MouthInner'
            Add-PixelRect $Graphics $Brushes $preview_scale 32 44 18 2 'MouthAccent'
            Add-PixelRect $Graphics $Brushes $preview_scale 36 46 10 1 'MouthAccent'
        }
    }
}

function Convert-FeatureX {
    param([int]$X, [double]$ScaleX)

    return [int][math]::Round(40 + (($X - 40) * $ScaleX))
}

function Draw-PoutMouth {
    param(
        [System.Drawing.Graphics]$Graphics,
        [hashtable]$Brushes,
        [pscustomobject]$Theme,
        [ValidateSet('compress', 'expand')][string]$Phase
    )

    switch ($Theme.Pack) {
        'icebox' {
            Add-PixelRect $Graphics $Brushes $preview_scale 35 38 12 8 'MouthCover'
            if ('compress' -eq $Phase) {
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(38, 40), @(43, 40), @(44, 41), @(42, 43), @(39, 43), @(37, 42), @(37, 41)
                ) 'MouthEdge'
                Add-PixelRect $Graphics $Brushes $preview_scale 39 41 3 1 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 40 42 1 1 'MouthAccent'
            }
            else {
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(37, 39), @(44, 39), @(45, 40), @(45, 44), @(44, 45), @(37, 45), @(36, 44), @(36, 40)
                ) 'MouthEdge'
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(38, 41), @(43, 41), @(44, 42), @(43, 44), @(38, 44), @(37, 42)
                ) 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 40 43 2 1 'MouthAccent'
            }
        }
        'crimson_slime' {
            Add-PixelRect $Graphics $Brushes $preview_scale 35 35 12 10 'MouthCover'
            if ('compress' -eq $Phase) {
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(37, 37), @(44, 37), @(45, 39), @(43, 41), @(38, 41), @(36, 39)
                ) 'MouthEdge'
                Add-PixelRect $Graphics $Brushes $preview_scale 38 39 5 1 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 40 40 2 1 'MouthAccent'
            }
            else {
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(38, 36), @(43, 36), @(45, 38), @(45, 42), @(43, 44), @(38, 44), @(36, 42), @(36, 38)
                ) 'MouthEdge'
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(39, 38), @(42, 38), @(44, 39), @(44, 41), @(42, 43), @(39, 43), @(37, 41), @(37, 39)
                ) 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 40 42 2 1 'MouthAccent'
            }
        }
        'jade_frog' {
            Add-PixelRect $Graphics $Brushes $preview_scale 32 39 17 9 'MouthCover'
            if ('compress' -eq $Phase) {
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(34, 41), @(38, 41), @(40, 42), @(42, 41), @(47, 41),
                    @(47, 44), @(43, 44), @(41, 45), @(39, 44), @(34, 44)
                ) 'MouthEdge'
                Add-PixelRect $Graphics $Brushes $preview_scale 36 43 9 1 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 40 44 2 1 'MouthAccent'
            }
            else {
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(34, 40), @(47, 40), @(49, 42), @(49, 45), @(47, 47), @(34, 47), @(32, 45), @(32, 42)
                ) 'MouthEdge'
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(35, 42), @(46, 42), @(47, 43), @(47, 45), @(34, 45), @(34, 43)
                ) 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 38 45 6 1 'MouthAccent'
            }
        }
        'bone_skull' {
            Add-PixelRect $Graphics $Brushes $preview_scale 34 41 13 8 'MouthCover'
            if ('compress' -eq $Phase) {
                Add-PixelRect $Graphics $Brushes $preview_scale 35 42 11 5 'MouthEdge'
                Add-PixelRect $Graphics $Brushes $preview_scale 36 43 9 3 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 37 43 1 1 'MouthAccent'
                Add-PixelRect $Graphics $Brushes $preview_scale 40 43 1 1 'MouthAccent'
                Add-PixelRect $Graphics $Brushes $preview_scale 43 43 1 1 'MouthAccent'
            }
            else {
                Add-PixelRect $Graphics $Brushes $preview_scale 34 41 13 7 'MouthEdge'
                Add-PixelRect $Graphics $Brushes $preview_scale 35 42 11 5 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 37 42 1 1 'MouthAccent'
                Add-PixelRect $Graphics $Brushes $preview_scale 40 42 1 1 'MouthAccent'
                Add-PixelRect $Graphics $Brushes $preview_scale 43 42 1 1 'MouthAccent'
                Add-PixelRect $Graphics $Brushes $preview_scale 37 46 1 1 'MouthAccent'
                Add-PixelRect $Graphics $Brushes $preview_scale 40 46 1 1 'MouthAccent'
                Add-PixelRect $Graphics $Brushes $preview_scale 43 46 1 1 'MouthAccent'
            }
        }
        'cobalt_owl' {
            Add-PixelRect $Graphics $Brushes $preview_scale 35 34 11 13 'MouthCover'
            if ('compress' -eq $Phase) {
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(40, 36), @(43, 40), @(40, 44), @(37, 40)
                ) 'MouthEdge'
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(40, 38), @(42, 40), @(40, 42), @(38, 40)
                ) 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 40 42 1 1 'MouthAccent'
            }
            else {
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(40, 35), @(45, 40), @(40, 46), @(35, 40)
                ) 'MouthEdge'
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(40, 37), @(43, 40), @(40, 44), @(37, 40)
                ) 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 40 44 1 1 'MouthAccent'
            }
        }
        'magenta_octopus' {
            Add-PixelRect $Graphics $Brushes $preview_scale 36 34 10 10 'MouthCover'
            if ('compress' -eq $Phase) {
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(39, 36), @(42, 36), @(44, 38), @(42, 40), @(39, 40), @(37, 38)
                ) 'MouthEdge'
                Add-PixelRect $Graphics $Brushes $preview_scale 39 38 3 1 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 40 39 1 1 'MouthAccent'
            }
            else {
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(39, 35), @(42, 35), @(45, 38), @(45, 41), @(42, 44), @(39, 44), @(36, 41), @(36, 38)
                ) 'MouthEdge'
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(40, 37), @(41, 37), @(43, 39), @(43, 40), @(41, 42), @(40, 42), @(38, 40), @(38, 39)
                ) 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 40 41 2 1 'MouthAccent'
            }
        }
        'silver_husky' {
            Add-PixelRect $Graphics $Brushes $preview_scale 37 42 8 7 'MouthCover'
            if ('compress' -eq $Phase) {
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(38, 42), @(44, 42), @(44, 44), @(42, 45), @(40, 45), @(38, 44)
                ) 'MouthEdge'
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(39, 43), @(43, 43), @(43, 44), @(39, 44)
                ) 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 41 44 1 1 'MouthAccent'
            }
            else {
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(38, 42), @(44, 42), @(46, 44), @(46, 46),
                    @(44, 47), @(38, 47), @(36, 46), @(36, 44)
                ) 'MouthEdge'
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(39, 44), @(43, 44), @(44, 45), @(43, 46),
                    @(39, 46), @(38, 45)
                ) 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 40 46 2 1 'MouthAccent'
            }
        }
        'amber_duck' {
            Add-PixelRect $Graphics $Brushes $preview_scale 30 34 22 13 'MouthCover'
            if ('compress' -eq $Phase) {
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(33, 37), @(37, 37), @(37, 36), @(44, 36), @(44, 37), @(49, 37),
                    @(49, 43), @(45, 43), @(45, 44), @(36, 44), @(36, 43), @(33, 43)
                ) 'MouthEdge'
                Add-PixelRect $Graphics $Brushes $preview_scale 35 38 12 2 'MouthAccent'
                Add-PixelRect $Graphics $Brushes $preview_scale 36 40 10 2 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 35 42 12 1 'MouthAccent'
            }
            else {
                Add-PixelPolygon $Graphics $Brushes $preview_scale @(
                    @(31, 36), @(36, 36), @(36, 34), @(45, 34), @(45, 36), @(51, 36),
                    @(51, 44), @(46, 44), @(46, 46), @(35, 46), @(35, 44), @(31, 44)
                ) 'MouthEdge'
                Add-PixelRect $Graphics $Brushes $preview_scale 33 37 16 3 'MouthAccent'
                Add-PixelRect $Graphics $Brushes $preview_scale 34 40 14 3 'MouthInner'
                Add-PixelRect $Graphics $Brushes $preview_scale 33 43 16 1 'MouthAccent'
            }
        }
    }
}

function Draw-Pout {
    param(
        [System.Drawing.Graphics]$Graphics,
        [hashtable]$Brushes,
        [pscustomobject]$Theme,
        [ValidateSet('compress', 'expand')][string]$Phase
    )

    $scale_x = if ('compress' -eq $Phase) { 0.90 } else { 1.10 }
    $left_cheek_x = Convert-FeatureX $Theme.CheekLeft[0] $scale_x
    $right_cheek_x = Convert-FeatureX $Theme.CheekRight[0] $scale_x

    if ('compress' -eq $Phase) {
        Add-PixelRect $Graphics $Brushes $preview_scale ($left_cheek_x + 1) $Theme.CheekLeft[1] 2 1 'Cheek'
        Add-PixelRect $Graphics $Brushes $preview_scale ($right_cheek_x - 1) $Theme.CheekRight[1] 2 1 'Cheek'
    }
    else {
        Add-PixelRect $Graphics $Brushes $preview_scale ($left_cheek_x - 1) ($Theme.CheekLeft[1] - 1) 4 3 'Outline'
        Add-PixelRect $Graphics $Brushes $preview_scale $left_cheek_x $Theme.CheekLeft[1] 2 1 'Cheek'
        Add-PixelRect $Graphics $Brushes $preview_scale ($right_cheek_x - 1) ($Theme.CheekRight[1] - 1) 4 3 'Outline'
        Add-PixelRect $Graphics $Brushes $preview_scale $right_cheek_x $Theme.CheekRight[1] 2 1 'Cheek'
    }

    Draw-PoutMouth $Graphics $Brushes $Theme $Phase

    foreach ($eye in @($Theme.EyeLeft, $Theme.EyeRight)) {
        $eye_x = Convert-FeatureX $eye[0] $scale_x
        $eye_width = [math]::Max(2, [int][math]::Round($eye[2] * $scale_x))
        Add-PixelRect $Graphics $Brushes $preview_scale ($eye_x - 1) ($eye[1] - 1) ($eye_width + 2) ($eye[3] + 2) 'Cover'
        Add-PixelRect $Graphics $Brushes $preview_scale $eye_x ($eye[1] + $eye[3] - 2) $eye_width 1 'Eye'
    }
}

function New-StatesOverview {
    param([pscustomobject]$Theme, [string[]]$ImagePaths)

    $cell_width = $logical_width * $overview_scale
    $cell_height = $logical_height * $overview_scale
    $overview = [System.Drawing.Bitmap]::new(
        $cell_width * 4,
        $cell_height * 3,
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

        $output_path = Join-Path $concepts_root ($Theme.Pack + '\' + $Theme.Pack + '_states_overview_1x.png')
        $overview.Save($output_path, [System.Drawing.Imaging.ImageFormat]::Png)
        return $output_path
    }
    finally {
        $graphics.Dispose()
        $overview.Dispose()
    }
}

function Assert-TalkMouthColors {
    param([pscustomobject[]]$Themes)

    $samples = @{
        icebox = @(40, 41)
        crimson_slime = @(39, 39)
        jade_frog = @(40, 42)
        cobalt_owl = @(40, 40)
        magenta_octopus = @(40, 38)
        silver_husky = @(41, 44)
        amber_duck = @(40, 40)
    }

    foreach ($theme in $Themes) {
        if (-not $samples.ContainsKey($theme.Pack)) {
            continue
        }

        $path = Join-Path $concepts_root ($theme.Pack + '\' + $theme.Pack + '_state_talk_open_preview_3x.png')
        $image = [System.Drawing.Bitmap]::FromFile($path)
        try {
            $sample = $samples[$theme.Pack]
            $actual = $image.GetPixel(($sample[0] * $preview_scale) + 6, ($sample[1] * $preview_scale) + 6)
            $expected = [System.Drawing.ColorTranslator]::FromHtml($theme.MouthInner)
            if ($actual.ToArgb() -ne $expected.ToArgb()) {
                throw "Talk mouth color regression for $($theme.Pack): expected $($theme.MouthInner), actual $($actual.Name)"
            }
        }
        finally {
            $image.Dispose()
        }
    }
}

function Assert-FrozenTalkFrames {
    $expected_hashes = @{
        'icebox|talk_closed' = '551346B841FCEFB4F9501C8743D0035FDE385FBB8BFBBEBDE2099A0EE2B45A5E'
        'crimson_slime|talk_closed' = 'AB77B282932C6D2F7CB469C160A4798520A874A0808D3ACF8052AB87EF205BFA'
        'crimson_slime|talk_open' = '2A5C17C939029217934701666F48F620DBF1B83973EEDA0E91FDCC4C2BEF8296'
        'jade_frog|talk_closed' = '837371B3C5B2EF2F515C6C413CB1ADD9223C0EEB3177D8363AE256D885C333C2'
        'jade_frog|talk_open' = '1A93C78A7FFDF248BB903AFF06DF0070C2BEFCFEC13F7A6AE0A6A244EFC93DC5'
        'bone_skull|talk_closed' = 'C1AAB68516F1CA952A734D425A2F34AAE6F5A4E3E9D1AB84DA004967BA8A0133'
        'bone_skull|talk_open' = '850582D8DCDF7BAABF0D3F709E2C7A387E573B52F3E5C611938C84B4CA4E4E0A'
        'cobalt_owl|talk_closed' = '97434D6940A7462FB725D96F846FBCC10F5D112BAD287D8E65EB5D3CBB82D8C1'
        'cobalt_owl|talk_open' = '90352A4ED05E6B0605D4A5319432BBA5F820443BCEBDE53A2E4A900D5BB0FC3C'
        'magenta_octopus|talk_closed' = '8EF59F526EA53AD32D8C47DFC5462E87250343D4A58F3DC552EFF63CE737FF0F'
        'magenta_octopus|talk_open' = '38C632E1D7E8D437BB9E89D5E005FD681949C96ABB6D692CABE3A9BCFBC73DE0'
        'silver_husky|talk_closed' = '97514E7E47F99E98861D5478D7B87F277D88EA6913910436B90695F9EF38C6AF'
        'amber_duck|talk_closed' = '2B85FFEA2504E81AECACC38F74796ECAAB3F55ECA7FF0D14CD60FDBBA378B2C0'
        'amber_duck|talk_open' = 'C1B46E13C521BEC47911F1DFBBA674A8FF6AB440F8DB7A4DC49D95DE798065CB'
    }

    foreach ($entry in $expected_hashes.GetEnumerator()) {
        $parts = $entry.Key.Split('|')
        $path = Join-Path $concepts_root ($parts[0] + '\' + $parts[0] + '_state_' + $parts[1] + '_preview_3x.png')
        $actual_hash = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
        if ($actual_hash -ne $entry.Value) {
            throw "Frozen talk frame regression for $($entry.Key): expected $($entry.Value), actual $actual_hash"
        }
    }
}

function Assert-PoutMouthColors {
    param([pscustomobject[]]$Themes)

    $samples = @{
        icebox = @{ compress = @(40, 41); expand = @(40, 42) }
        crimson_slime = @{ compress = @(40, 39); expand = @(40, 40) }
        jade_frog = @{ compress = @(40, 43); expand = @(40, 43) }
        bone_skull = @{ compress = @(40, 44); expand = @(40, 44) }
        cobalt_owl = @{ compress = @(40, 40); expand = @(40, 40) }
        magenta_octopus = @{ compress = @(40, 38); expand = @(40, 39) }
        silver_husky = @{ compress = @(40, 43); expand = @(40, 45) }
        amber_duck = @{ compress = @(40, 40); expand = @(40, 41) }
    }

    foreach ($theme in $Themes) {
        foreach ($phase in @('compress', 'expand')) {
            $path = Join-Path $concepts_root ($theme.Pack + '\' + $theme.Pack + '_state_touch_pout_' + $phase + '_preview_3x.png')
            $image = [System.Drawing.Bitmap]::FromFile($path)
            try {
                $sample = $samples[$theme.Pack][$phase]
                $actual = $image.GetPixel(($sample[0] * $preview_scale) + 6, ($sample[1] * $preview_scale) + 6)
                $expected = [System.Drawing.ColorTranslator]::FromHtml($theme.MouthInner)
                if ($actual.ToArgb() -ne $expected.ToArgb()) {
                    throw "Pout mouth color regression for $($theme.Pack)/$phase`: expected $($theme.MouthInner), actual $($actual.Name)"
                }
            }
            finally {
                $image.Dispose()
            }
        }
    }
}

function Assert-PoutEyeTreatment {
    param([pscustomobject[]]$Themes)

    foreach ($theme in $Themes) {
        foreach ($phase in @('compress', 'expand')) {
            $scale_x = if ('compress' -eq $phase) { 0.90 } else { 1.10 }
            $path = Join-Path $concepts_root ($theme.Pack + '\' + $theme.Pack + '_state_touch_pout_' + $phase + '_preview_3x.png')
            $image = [System.Drawing.Bitmap]::FromFile($path)
            try {
                foreach ($eye in @($theme.EyeLeft, $theme.EyeRight)) {
                    $eye_x = Convert-FeatureX $eye[0] $scale_x
                    $eye_width = [math]::Max(2, [int][math]::Round($eye[2] * $scale_x))
                    $legacy_width = [math]::Max(2, $eye[2] - 1)
                    $legacy_y = $eye[1] - 2
                    $pupil_color = [System.Drawing.ColorTranslator]::FromHtml($theme.Pupil)
                    for ($offset = 0; $offset -lt $legacy_width; $offset++) {
                        $actual = $image.GetPixel((($eye_x + $offset) * $preview_scale) + 6, ($legacy_y * $preview_scale) + 6)
                        if ($actual.ToArgb() -eq $pupil_color.ToArgb()) {
                            throw "Legacy pout eye line regression for $($theme.Pack)/$phase at $($eye_x + $offset),$legacy_y"
                        }
                    }

                    $squint_y = $eye[1] + $eye[3] - 2
                    $eye_color = [System.Drawing.ColorTranslator]::FromHtml($theme.Eye)
                    $cover_color = [System.Drawing.ColorTranslator]::FromHtml($theme.Cover)
                    for ($y = $eye[1] - 1; $y -le $eye[1] + $eye[3]; $y++) {
                        for ($x = $eye_x - 1; $x -le $eye_x + $eye_width; $x++) {
                            $actual = $image.GetPixel(($x * $preview_scale) + 6, ($y * $preview_scale) + 6)
                            $expected = if (($squint_y -eq $y) -and ($x -ge $eye_x) -and ($x -lt $eye_x + $eye_width)) {
                                $eye_color
                            }
                            else {
                                $cover_color
                            }
                            if ($actual.ToArgb() -ne $expected.ToArgb()) {
                                throw "Pout eye treatment regression for $($theme.Pack)/$phase at $x,$y`: expected $($expected.Name), actual $($actual.Name)"
                            }
                        }
                    }
                }
            }
            finally {
                $image.Dispose()
            }
        }
    }
}

function Assert-DuckEyeDirection {
    param([pscustomobject[]]$Themes)

    $theme = $Themes | Where-Object { 'amber_duck' -eq $_.Pack } | Select-Object -First 1
    if ($null -eq $theme) {
        throw 'amber_duck theme is required for eye direction regression checks'
    }

    $expectations = @{
        listen_focus = @{ OffsetX = 2; OffsetY = 2 }
        think = @{ OffsetX = 4; OffsetY = 1 }
        turn_gaze_left = @{ OffsetX = 1; OffsetY = 2 }
        turn_gaze_right = @{ OffsetX = 3; OffsetY = 2 }
    }
    $eye_color = [System.Drawing.ColorTranslator]::FromHtml($theme.Eye)
    $pupil_color = [System.Drawing.ColorTranslator]::FromHtml($theme.Pupil)

    foreach ($entry in $expectations.GetEnumerator()) {
        $path = Join-Path $concepts_root ($theme.Pack + '\' + $theme.Pack + '_state_' + $entry.Key + '_preview_3x.png')
        $image = [System.Drawing.Bitmap]::FromFile($path)
        try {
            foreach ($eye in @($theme.EyeLeft, $theme.EyeRight)) {
                for ($y = 0; $y -lt $eye[3]; $y++) {
                    for ($x = 0; $x -lt $eye[2]; $x++) {
                        $in_pupil = ($x -ge $entry.Value.OffsetX) -and
                                    ($x -lt ($entry.Value.OffsetX + 2)) -and
                                    ($y -ge $entry.Value.OffsetY) -and
                                    ($y -lt ($entry.Value.OffsetY + 3))
                        $expected = if ($in_pupil) { $pupil_color } else { $eye_color }
                        $actual = $image.GetPixel(
                            (($eye[0] + $x) * $preview_scale) + 6,
                            (($eye[1] + $y) * $preview_scale) + 6
                        )
                        if ($actual.ToArgb() -ne $expected.ToArgb()) {
                            throw "Duck eye direction regression for $($entry.Key) at $($eye[0] + $x),$($eye[1] + $y): expected $($expected.Name), actual $($actual.Name)"
                        }
                    }
                }
            }
        }
        finally {
            $image.Dispose()
        }
    }
}

function Assert-DuckTalkOpening {
    param([pscustomobject[]]$Themes)

    $theme = $Themes | Where-Object { 'amber_duck' -eq $_.Pack } | Select-Object -First 1
    if ($null -eq $theme) {
        throw 'amber_duck theme is required for talk opening regression checks'
    }

    $path = Join-Path $concepts_root 'amber_duck\amber_duck_state_talk_open_preview_3x.png'
    $image = [System.Drawing.Bitmap]::FromFile($path)
    $mouth_inner = [System.Drawing.ColorTranslator]::FromHtml($theme.MouthInner)
    try {
        for ($y = 40; $y -le 43; $y++) {
            for ($x = 32; $x -le 49; $x++) {
                $actual = $image.GetPixel(
                    ($x * $preview_scale) + 6,
                    ($y * $preview_scale) + 6
                )
                if ($actual.ToArgb() -ne $mouth_inner.ToArgb()) {
                    throw "Duck talk opening regression at $x,$y`: expected $($mouth_inner.Name), actual $($actual.Name)"
                }
            }

            foreach ($edge_x in @(31, 50)) {
                $actual_edge = $image.GetPixel(
                    ($edge_x * $preview_scale) + 6,
                    ($y * $preview_scale) + 6
                )
                $expected_edge = [System.Drawing.ColorTranslator]::FromHtml($theme.MouthEdge)
                if ($actual_edge.ToArgb() -ne $expected_edge.ToArgb()) {
                    throw "Duck talk side edge regression at $edge_x,$y`: expected $($expected_edge.Name), actual $($actual_edge.Name)"
                }
            }
        }

        $mouth_accent = [System.Drawing.ColorTranslator]::FromHtml($theme.MouthAccent)
        for ($x = 32; $x -le 49; $x++) {
            $actual = $image.GetPixel(
                ($x * $preview_scale) + 6,
                (39 * $preview_scale) + 6
            )
            if ($actual.ToArgb() -ne $mouth_accent.ToArgb()) {
                throw "Duck talk internal line regression at $x,39: expected $($mouth_accent.Name), actual $($actual.Name)"
            }
        }
    }
    finally {
        $image.Dispose()
    }
}

$pack_overview_paths = @()

foreach ($theme in $themes) {
    $source_path = Join-Path $concepts_root $theme.Source
    if (-not (Test-Path -LiteralPath $source_path)) {
        throw "Missing approved baseline image: $source_path"
    }

    $source = [System.Drawing.Image]::FromFile($source_path)
    $brushes = New-BrushMap $theme
    $state_paths = @()
    try {
        foreach ($state in $states) {
            $frame = New-Frame $source $brushes
            try {
                switch ($state) {
                    'blink' { Draw-Blink $frame.Graphics $brushes $theme }
                    'idle_sway_left_up' { Move-Subject $frame.Graphics $source $brushes -1 -1 }
                    'idle_sway_right_up' { Move-Subject $frame.Graphics $source $brushes 1 -1 }
                    'listen_focus' { Draw-Focus $frame.Graphics $brushes $theme }
                    'think' { Draw-Think $frame.Graphics $brushes $theme }
                    'turn_gaze_left' { Draw-Gaze $frame.Graphics $brushes $theme 'left' }
                    'turn_gaze_right' { Draw-Gaze $frame.Graphics $brushes $theme 'right' }
                    'talk_closed' { Draw-Talk $frame.Graphics $brushes $theme $false }
                    'talk_open' { Draw-Talk $frame.Graphics $brushes $theme $true }
                    'touch_pout_compress' {
                        Move-Subject $frame.Graphics $source $brushes 0 0 0.90
                        Draw-Pout $frame.Graphics $brushes $theme 'compress'
                    }
                    'touch_pout_expand' {
                        Move-Subject $frame.Graphics $source $brushes 0 0 1.10
                        Draw-Pout $frame.Graphics $brushes $theme 'expand'
                    }
                }

                $output_path = Join-Path $concepts_root ($theme.Pack + '\' + $theme.Pack + '_state_' + $state + '_preview_3x.png')
                $frame.Bitmap.Save($output_path, [System.Drawing.Imaging.ImageFormat]::Png)
                $state_paths += $output_path
            }
            finally {
                $frame.Graphics.Dispose()
                $frame.Bitmap.Dispose()
            }
        }

        $overview_path = New-StatesOverview $theme $state_paths
        $pack_overview_paths += $overview_path
        Write-Output $overview_path
    }
    finally {
        $source.Dispose()
        foreach ($brush in $brushes.Values) {
            $brush.Dispose()
        }
    }
}

Assert-TalkMouthColors $themes
Assert-FrozenTalkFrames
Assert-PoutMouthColors $themes
Assert-PoutEyeTreatment $themes
Assert-DuckEyeDirection $themes
Assert-DuckTalkOpening $themes

$collection = [System.Drawing.Bitmap]::new(
    2560,
    2880,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
)
$collection_graphics = [System.Drawing.Graphics]::FromImage($collection)
$collection_graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$collection_graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$collection_graphics.Clear([System.Drawing.ColorTranslator]::FromHtml($background))
try {
    for ($index = 0; $index -lt $pack_overview_paths.Count; $index++) {
        $image = [System.Drawing.Image]::FromFile($pack_overview_paths[$index])
        try {
            $x = ($index % 2) * 1280
            $y = [math]::Floor($index / 2) * 720
            $collection_graphics.DrawImage($image, $x, $y, 1280, 720)
        }
        finally {
            $image.Dispose()
        }
    }

    $collection_path = Join-Path $concepts_root 'expression_states_overview_2x4.png'
    $collection.Save($collection_path, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Output $collection_path
}
finally {
    $collection_graphics.Dispose()
    $collection.Dispose()
}

$mouth_review_states = @('talk_closed', 'talk_open', 'touch_pout_compress', 'touch_pout_expand')
$mouth_review_scale = 3
$mouth_review = [System.Drawing.Bitmap]::new(
    $logical_width * $mouth_review_scale * $themes.Count,
    $logical_height * $mouth_review_scale * $mouth_review_states.Count,
    [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
)
$mouth_review_graphics = [System.Drawing.Graphics]::FromImage($mouth_review)
$mouth_review_graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
$mouth_review_graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
$mouth_review_graphics.Clear([System.Drawing.ColorTranslator]::FromHtml($background))
try {
    for ($pack_index = 0; $pack_index -lt $themes.Count; $pack_index++) {
        $theme = $themes[$pack_index]
        for ($state_index = 0; $state_index -lt $mouth_review_states.Count; $state_index++) {
            $state = $mouth_review_states[$state_index]
            $path = Join-Path $concepts_root ($theme.Pack + '\' + $theme.Pack + '_state_' + $state + '_preview_3x.png')
            $image = [System.Drawing.Image]::FromFile($path)
            try {
                $x = $pack_index * $logical_width * $mouth_review_scale
                $y = $state_index * $logical_height * $mouth_review_scale
                $mouth_review_graphics.DrawImage(
                    $image,
                    $x,
                    $y,
                    $logical_width * $mouth_review_scale,
                    $logical_height * $mouth_review_scale
                )
            }
            finally {
                $image.Dispose()
            }
        }
    }

    $mouth_review_path = Join-Path $concepts_root 'expression_mouth_review_8x4.png'
    $mouth_review.Save($mouth_review_path, [System.Drawing.Imaging.ImageFormat]::Png)
    Write-Output $mouth_review_path
}
finally {
    $mouth_review_graphics.Dispose()
    $mouth_review.Dispose()
}
