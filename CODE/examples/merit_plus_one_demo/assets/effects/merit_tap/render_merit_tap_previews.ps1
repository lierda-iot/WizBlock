param(
    [string]$InputPath = (Join-Path $PSScriptRoot '..\..\expressions\concepts\icebox\icebox_state_idle_preview_3x.png'),
    [string]$OutputDirectory = $PSScriptRoot,
    [string]$OnlyFrame = ''
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$canvas_width = 320
$canvas_height = 240
$caption = '功德+1'

function New-ArgbColor {
    param(
        [int]$Alpha,
        [string]$HtmlColor
    )

    $rgb = [System.Drawing.ColorTranslator]::FromHtml($HtmlColor)
    return [System.Drawing.Color]::FromArgb($Alpha, $rgb.R, $rgb.G, $rgb.B)
}

function New-PixelBubblePath {
    param(
        [int]$X,
        [int]$Y,
        [int]$Width,
        [int]$Height,
        [int]$Corner = 6,
        [ValidateSet('soft_round', 'block_ellipse')]
        [string]$Shape = 'soft_round'
    )

    $path = [System.Drawing.Drawing2D.GraphicsPath]::new()
    if ('block_ellipse' -eq $Shape) {
        if ($Width -lt 72) {
            $compact_step_x = [math]::Max(4, [int][math]::Floor($Width * 0.12))
            $compact_step_y = [math]::Max(3, [int][math]::Floor($Height * 0.16))
            $compact_top_y = [math]::Max(1, [int][math]::Floor($compact_step_y / 2))
            $compact_peak_left_x = [int][math]::Floor($Width * 0.24)
            $compact_peak_right_x = [int][math]::Floor($Width * 0.68)
            $compact_right_x = $Width - (2 * $compact_step_x) - 2
            $compact_bottom_x = [int][math]::Floor($Width * 0.62)
            $compact_points = [System.Drawing.Point[]]@(
                [System.Drawing.Point]::new($X + $compact_step_x + 2, $Y + $compact_top_y),
                [System.Drawing.Point]::new($X + $compact_peak_left_x, $Y + $compact_top_y),
                [System.Drawing.Point]::new($X + $compact_peak_left_x, $Y - 1),
                [System.Drawing.Point]::new($X + $compact_peak_right_x, $Y - 1),
                [System.Drawing.Point]::new($X + $compact_peak_right_x, $Y + 1),
                [System.Drawing.Point]::new($X + $compact_right_x, $Y + 1),
                [System.Drawing.Point]::new($X + $compact_right_x, $Y + $compact_step_y),
                [System.Drawing.Point]::new($X + $Width - $compact_step_x, $Y + $compact_step_y),
                [System.Drawing.Point]::new($X + $Width - $compact_step_x, $Y + (2 * $compact_step_y)),
                [System.Drawing.Point]::new($X + $Width, $Y + (2 * $compact_step_y)),
                [System.Drawing.Point]::new($X + $Width, $Y + $Height - (2 * $compact_step_y)),
                [System.Drawing.Point]::new($X + $Width - $compact_step_x, $Y + $Height - (2 * $compact_step_y)),
                [System.Drawing.Point]::new($X + $Width - $compact_step_x, $Y + $Height - $compact_step_y),
                [System.Drawing.Point]::new($X + $compact_right_x, $Y + $Height - $compact_step_y),
                [System.Drawing.Point]::new($X + $compact_right_x, $Y + $Height - 1),
                [System.Drawing.Point]::new($X + $compact_bottom_x, $Y + $Height - 1),
                [System.Drawing.Point]::new($X + $compact_bottom_x, $Y + $Height),
                [System.Drawing.Point]::new($X + $compact_step_x + 2, $Y + $Height),
                [System.Drawing.Point]::new($X + $compact_step_x + 2, $Y + $Height - 2),
                [System.Drawing.Point]::new($X + 2, $Y + $Height - 2),
                [System.Drawing.Point]::new($X + 2, $Y + $Height - $compact_step_y),
                [System.Drawing.Point]::new($X, $Y + $Height - $compact_step_y),
                [System.Drawing.Point]::new($X, $Y + (2 * $compact_step_y)),
                [System.Drawing.Point]::new($X + 2, $Y + (2 * $compact_step_y)),
                [System.Drawing.Point]::new($X + 2, $Y + $compact_step_y),
                [System.Drawing.Point]::new($X + $compact_step_x + 2, $Y + $compact_step_y),
                [System.Drawing.Point]::new($X + $compact_step_x + 2, $Y + $compact_top_y)
            )
            $path.AddPolygon($compact_points)
            return $path
        }

        $step_x = [math]::Max(8, [int][math]::Floor($Corner * 0.5))
        $step_y = [math]::Max(6, [int][math]::Floor($Corner * 0.34))
        $top_left_y = [math]::Max(2, [int][math]::Floor($step_y / 2))
        $top_peak_lift = 2
        $top_peak_left_x = [int][math]::Floor($Width * 0.24)
        $top_peak_right_x = [int][math]::Floor($Width * 0.68)
        $top_right_y = [math]::Max(1, [int][math]::Floor($top_left_y / 2))
        $bottom_break_x = [int][math]::Floor($Width * 0.62)
        $points = [System.Drawing.Point[]]@(
            [System.Drawing.Point]::new($X + (2 * $step_x) + 4, $Y + $top_left_y),
            [System.Drawing.Point]::new($X + $top_peak_left_x, $Y + $top_left_y),
            [System.Drawing.Point]::new($X + $top_peak_left_x, $Y - $top_peak_lift),
            [System.Drawing.Point]::new($X + $top_peak_right_x, $Y - $top_peak_lift),
            [System.Drawing.Point]::new($X + $top_peak_right_x, $Y + $top_right_y),
            [System.Drawing.Point]::new($X + $Width - (2 * $step_x) - 3, $Y + $top_right_y),
            [System.Drawing.Point]::new($X + $Width - (2 * $step_x) - 3, $Y + $step_y - 1),
            [System.Drawing.Point]::new($X + $Width - $step_x, $Y + $step_y - 1),
            [System.Drawing.Point]::new($X + $Width - $step_x, $Y + (2 * $step_y)),
            [System.Drawing.Point]::new($X + $Width, $Y + (2 * $step_y)),
            [System.Drawing.Point]::new($X + $Width, $Y + $Height - (2 * $step_y) - 1),
            [System.Drawing.Point]::new($X + $Width - $step_x - 2, $Y + $Height - (2 * $step_y) - 1),
            [System.Drawing.Point]::new($X + $Width - $step_x - 2, $Y + $Height - $step_y),
            [System.Drawing.Point]::new($X + $Width - (2 * $step_x) - 5, $Y + $Height - $step_y),
            [System.Drawing.Point]::new($X + $Width - (2 * $step_x) - 5, $Y + $Height - 2),
            [System.Drawing.Point]::new($X + $bottom_break_x, $Y + $Height - 2),
            [System.Drawing.Point]::new($X + $bottom_break_x, $Y + $Height),
            [System.Drawing.Point]::new($X + (2 * $step_x) + 2, $Y + $Height),
            [System.Drawing.Point]::new($X + (2 * $step_x) + 2, $Y + $Height - 3),
            [System.Drawing.Point]::new($X + $step_x, $Y + $Height - 3),
            [System.Drawing.Point]::new($X + $step_x, $Y + $Height - $step_y - 1),
            [System.Drawing.Point]::new($X + 2, $Y + $Height - $step_y - 1),
            [System.Drawing.Point]::new($X + 2, $Y + $Height - (2 * $step_y) - 2),
            [System.Drawing.Point]::new($X, $Y + $Height - (2 * $step_y) - 2),
            [System.Drawing.Point]::new($X, $Y + (2 * $step_y) + 2),
            [System.Drawing.Point]::new($X + 4, $Y + (2 * $step_y) + 2),
            [System.Drawing.Point]::new($X + 4, $Y + $step_y + 1),
            [System.Drawing.Point]::new($X + $step_x + 2, $Y + $step_y + 1),
            [System.Drawing.Point]::new($X + $step_x + 2, $Y + $top_left_y)
        )
    }
    else {
        $step = 2
        $half_corner = [math]::Max(3, [int][math]::Floor($Corner / 2))
        $points = [System.Drawing.Point[]]@(
            [System.Drawing.Point]::new($X + $Corner, $Y),
            [System.Drawing.Point]::new($X + $Width - $Corner, $Y),
            [System.Drawing.Point]::new($X + $Width - $half_corner, $Y + $step),
            [System.Drawing.Point]::new($X + $Width - $step, $Y + $half_corner),
            [System.Drawing.Point]::new($X + $Width, $Y + $Corner),
            [System.Drawing.Point]::new($X + $Width, $Y + $Height - $Corner),
            [System.Drawing.Point]::new($X + $Width - $step, $Y + $Height - $half_corner),
            [System.Drawing.Point]::new($X + $Width - $half_corner, $Y + $Height - $step),
            [System.Drawing.Point]::new($X + $Width - $Corner, $Y + $Height),
            [System.Drawing.Point]::new($X + $Corner, $Y + $Height),
            [System.Drawing.Point]::new($X + $half_corner, $Y + $Height - $step),
            [System.Drawing.Point]::new($X + $step, $Y + $Height - $half_corner),
            [System.Drawing.Point]::new($X, $Y + $Height - $Corner),
            [System.Drawing.Point]::new($X, $Y + $Corner),
            [System.Drawing.Point]::new($X + $step, $Y + $half_corner),
            [System.Drawing.Point]::new($X + $half_corner, $Y + $step)
        )
    }
    $path.AddPolygon($points)
    return $path
}

function Add-PixelGlow {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$X,
        [int]$Y,
        [int]$Width,
        [int]$Height,
        [int]$Strength,
        [ValidateSet('soft_round', 'block_ellipse')]
        [string]$Shape = 'soft_round'
    )

    $layers = if ('block_ellipse' -eq $Shape) {
        @(
            @{ Expand = 10; Alpha = [math]::Max(6, [int]($Strength * 0.18)) },
            @{ Expand = 5; Alpha = [math]::Max(12, [int]($Strength * 0.38)) }
        )
    }
    else {
        @(
            @{ Expand = 12; Alpha = [math]::Max(5, [int]($Strength * 0.16)) },
            @{ Expand = 8; Alpha = [math]::Max(8, [int]($Strength * 0.26)) },
            @{ Expand = 4; Alpha = [math]::Max(12, [int]($Strength * 0.42)) }
        )
    }

    foreach ($layer in $layers) {
        $expand = $layer.Expand
        $glow_corner = if ('block_ellipse' -eq $Shape) {
            [math]::Max(12, [int][math]::Floor(($Height + (2 * $expand)) * 0.38))
        }
        else {
            6 + $expand
        }
        $path = New-PixelBubblePath `
            -X ($X - $expand) `
            -Y ($Y - $expand) `
            -Width ($Width + (2 * $expand)) `
            -Height ($Height + (2 * $expand)) `
            -Corner $glow_corner `
            -Shape $Shape
        $brush = [System.Drawing.SolidBrush]::new((New-ArgbColor $layer.Alpha '#FACC15'))
        try {
            $Graphics.FillPath($brush, $path)
        }
        finally {
            $brush.Dispose()
            $path.Dispose()
        }
    }
}

function Add-PixelSparkle {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$X,
        [int]$Y,
        [int]$Size,
        [int]$Alpha
    )

    $glow = [System.Drawing.SolidBrush]::new((New-ArgbColor ([math]::Max(8, [int]($Alpha * 0.18))) '#FDE68A'))
    $core = [System.Drawing.SolidBrush]::new((New-ArgbColor $Alpha '#FFF7C2'))
    try {
        $Graphics.FillRectangle($glow, $X - $Size, $Y - $Size, (2 * $Size) + 1, (2 * $Size) + 1)
        $Graphics.FillRectangle($core, $X - 1, $Y - $Size, 3, (2 * $Size) + 1)
        $Graphics.FillRectangle($core, $X - $Size, $Y - 1, (2 * $Size) + 1, 3)
        $Graphics.FillRectangle($core, $X - 1, $Y - 1, 3, 3)
    }
    finally {
        $glow.Dispose()
        $core.Dispose()
    }
}

function Get-CaptionFontFamily {
    $installed = [System.Drawing.Text.InstalledFontCollection]::new()
    try {
        foreach ($name in @('Microsoft YaHei UI', 'Microsoft YaHei', 'SimHei')) {
            if ($installed.Families.Name -contains $name) {
                return $name
            }
        }
    }
    finally {
        $installed.Dispose()
    }

    throw 'A Chinese font is required to render the exact caption.'
}

function Add-PixelCaption {
    param(
        [System.Drawing.Graphics]$Graphics,
        [System.Drawing.RectangleF]$Bounds,
        [float]$FontSize,
        [int]$Alpha
    )

    $font = [System.Drawing.Font]::new(
        (Get-CaptionFontFamily),
        $FontSize,
        [System.Drawing.FontStyle]::Bold,
        [System.Drawing.GraphicsUnit]::Pixel
    )
    $format = [System.Drawing.StringFormat]::new()
    $format.Alignment = [System.Drawing.StringAlignment]::Center
    $format.LineAlignment = [System.Drawing.StringAlignment]::Center
    $shadow = [System.Drawing.SolidBrush]::new((New-ArgbColor ([math]::Min(210, $Alpha)) '#4A3200'))
    $foreground = [System.Drawing.SolidBrush]::new((New-ArgbColor $Alpha '#FFF4B0'))
    try {
        $Graphics.TextRenderingHint = [System.Drawing.Text.TextRenderingHint]::SingleBitPerPixelGridFit
        $shadow_bounds = [System.Drawing.RectangleF]::new(
            $Bounds.X + 1,
            $Bounds.Y + 1,
            $Bounds.Width,
            $Bounds.Height
        )
        $Graphics.DrawString($caption, $font, $shadow, $shadow_bounds, $format)
        $Graphics.DrawString($caption, $font, $foreground, $Bounds, $format)
    }
    finally {
        $foreground.Dispose()
        $shadow.Dispose()
        $format.Dispose()
        $font.Dispose()
    }
}

function Add-PixelBubble {
    param(
        [System.Drawing.Graphics]$Graphics,
        [int]$X,
        [int]$Y,
        [int]$Width,
        [int]$Height,
        [int]$TailX,
        [int]$TailY,
        [int]$FillAlpha,
        [int]$GlowStrength,
        [float]$FontSize,
        [int]$TextAlpha,
        [bool]$ShowText,
        [ValidateSet('soft_round', 'block_ellipse')]
        [string]$Shape = 'soft_round'
    )

    Add-PixelGlow $Graphics $X $Y $Width $Height $GlowStrength $Shape

    if ('block_ellipse' -eq $Shape) {
        $bubble_corner = [math]::Max(12, [int][math]::Floor($Height * 0.38))
        $inner_corner = [math]::Max(10, [int][math]::Floor(($Height - 8) * 0.38))
    }
    else {
        $bubble_corner = 8
        $inner_corner = 8
    }
    $path = New-PixelBubblePath $X $Y $Width $Height $bubble_corner $Shape
    $tail_points = [System.Drawing.Point[]]@(
        [System.Drawing.Point]::new($TailX - 8, $Y + $Height - 2),
        [System.Drawing.Point]::new($TailX + 5, $Y + $Height - 2),
        [System.Drawing.Point]::new($TailX, $TailY)
    )
    $inner_path = New-PixelBubblePath ($X + 4) ($Y + 4) ($Width - 8) ($Height - 8) $inner_corner $Shape
    $fill = [System.Drawing.SolidBrush]::new((New-ArgbColor $FillAlpha '#F6C945'))
    $inner = [System.Drawing.SolidBrush]::new((New-ArgbColor ([math]::Min(210, $FillAlpha + 34)) '#FFE58A'))
    $highlight = [System.Drawing.SolidBrush]::new((New-ArgbColor ([math]::Min(180, $FillAlpha + 14)) '#FFF6C8'))
    try {
        $Graphics.FillPath($fill, $path)
        $Graphics.FillPolygon($fill, $tail_points)
        $Graphics.FillPath($inner, $inner_path)
        $clip_state = $Graphics.Save()
        try {
            $Graphics.SetClip($inner_path, [System.Drawing.Drawing2D.CombineMode]::Replace)
            $Graphics.FillRectangle($highlight, $X + 18, $Y + 8, [math]::Max(8, $Width - 42), 4)
        }
        finally {
            $Graphics.Restore($clip_state)
        }
    }
    finally {
        $highlight.Dispose()
        $inner.Dispose()
        $fill.Dispose()
        $inner_path.Dispose()
        $path.Dispose()
    }

    if ($ShowText) {
        Add-PixelCaption `
            -Graphics $Graphics `
            -Bounds ([System.Drawing.RectangleF]::new($X + 2, $Y + 1, $Width - 4, $Height - 3)) `
            -FontSize $FontSize `
            -Alpha $TextAlpha
    }
}

function New-BaseFrame {
    param([System.Drawing.Image]$Source)

    $bitmap = [System.Drawing.Bitmap]::new(
        $canvas_width,
        $canvas_height,
        [System.Drawing.Imaging.PixelFormat]::Format32bppArgb
    )
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::None
    $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::NearestNeighbor
    $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::Half
    $graphics.DrawImage($Source, 0, 0, $canvas_width, $canvas_height)
    return @{ Bitmap = $bitmap; Graphics = $graphics }
}

$resolved_input = (Resolve-Path -LiteralPath $InputPath).Path
[System.IO.Directory]::CreateDirectory($OutputDirectory) | Out-Null
$source = [System.Drawing.Image]::FromFile($resolved_input)

try {
    $frames = @(
        @{
            Name = 'merit_bubble_01_seed.png'
            Render = {
                param($graphics)
                $seed_outer = [System.Drawing.SolidBrush]::new((New-ArgbColor 14 '#FACC15'))
                $seed_inner = [System.Drawing.SolidBrush]::new((New-ArgbColor 32 '#FACC15'))
                $seed = [System.Drawing.SolidBrush]::new((New-ArgbColor 132 '#FACC15'))
                try {
                    $graphics.FillRectangle($seed_outer, 218, 56, 26, 22)
                    $graphics.FillRectangle($seed_inner, 222, 60, 18, 14)
                    $graphics.FillRectangle($seed, 226, 63, 10, 6)
                    $graphics.FillRectangle($seed, 230, 69, 3, 5)
                }
                finally {
                    $seed.Dispose()
                    $seed_inner.Dispose()
                    $seed_outer.Dispose()
                }
                Add-PixelSparkle $graphics 231 65 4 238
                Add-PixelSparkle $graphics 220 57 2 150
            }
        },
        @{
            Name = 'merit_bubble_01a_sprout.png'
            Render = {
                param($graphics)
                Add-PixelBubble $graphics 208 53 50 26 231 84 94 102 9 0 $false 'block_ellipse'
                Add-PixelSparkle $graphics 218 48 3 204
                Add-PixelSparkle $graphics 260 58 2 138
            }
        },
        @{
            Name = 'merit_bubble_01b_growing.png'
            Render = {
                param($graphics)
                Add-PixelBubble $graphics 192 44 88 36 229 88 110 108 22 126 $true 'block_ellipse'
                Add-PixelSparkle $graphics 200 40 3 210
                Add-PixelSparkle $graphics 281 51 2 152
            }
        },
        @{
            Name = 'merit_bubble_02_readable.png'
            Render = {
                param($graphics)
                Add-PixelBubble $graphics 174 36 126 47 228 91 126 112 32 242 $true 'block_ellipse'
                Add-PixelSparkle $graphics 181 32 3 210
                Add-PixelSparkle $graphics 299 45 2 170
            }
        },
        @{
            Name = 'merit_bubble_02a_expand.png'
            Render = {
                param($graphics)
                Add-PixelBubble $graphics 163 29 142 56 226 93 106 98 36 210 $true 'block_ellipse'
                Add-PixelSparkle $graphics 171 25 3 176
                Add-PixelSparkle $graphics 304 39 2 130
            }
        },
        @{
            Name = 'merit_bubble_03_fade_max.png'
            Render = {
                param($graphics)
                Add-PixelBubble $graphics 154 24 154 61 225 94 82 82 39 160 $true 'block_ellipse'
                Add-PixelSparkle $graphics 164 21 3 120
                Add-PixelSparkle $graphics 305 34 2 92
            }
        },
        @{
            Name = 'merit_bubble_03a_dissolve.png'
            Render = {
                param($graphics)
                Add-PixelBubble $graphics 154 24 154 61 225 94 42 60 39 82 $true 'block_ellipse'
                Add-PixelSparkle $graphics 164 21 2 70
                Add-PixelSparkle $graphics 305 34 2 54
            }
        }
    )

    $rendered_count = 0
    foreach ($frame_spec in $frames) {
        if (('' -ne $OnlyFrame) -and ($frame_spec.Name -ne $OnlyFrame)) {
            continue
        }

        $frame = New-BaseFrame $source
        try {
            & $frame_spec.Render $frame.Graphics
            $output_path = Join-Path $OutputDirectory $frame_spec.Name
            $frame.Bitmap.Save($output_path, [System.Drawing.Imaging.ImageFormat]::Png)
            Write-Output (Resolve-Path -LiteralPath $output_path).Path
            $rendered_count++
        }
        finally {
            $frame.Graphics.Dispose()
            $frame.Bitmap.Dispose()
        }
    }

    if (0 -eq $rendered_count) {
        throw "Unknown frame: $OnlyFrame"
    }
}
finally {
    $source.Dispose()
}
