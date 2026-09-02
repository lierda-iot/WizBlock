param()

$ErrorActionPreference = 'Stop'

function Convert-ToRgb565 {
    param(
        [Parameter(Mandatory = $true)][int]$Red,
        [Parameter(Mandatory = $true)][int]$Green,
        [Parameter(Mandatory = $true)][int]$Blue
    )

    if ($Red -lt 0 -or $Red -gt 255 -or
        $Green -lt 0 -or $Green -gt 255 -or
        $Blue -lt 0 -or $Blue -gt 255) {
        throw 'RGB888 channel outside 0..255'
    }
    return (($Red -shr 3) -shl 11) -bor
           (($Green -shr 2) -shl 5) -bor
           ($Blue -shr 3)
}

function Convert-FromRgb565 {
    param([Parameter(Mandatory = $true)][int]$Value)

    $red5 = ($Value -shr 11) -band 0x1F
    $green6 = ($Value -shr 5) -band 0x3F
    $blue5 = $Value -band 0x1F
    return [pscustomobject]@{
        Red = ($red5 -shl 3) -bor ($red5 -shr 2)
        Green = ($green6 -shl 2) -bor ($green6 -shr 4)
        Blue = ($blue5 -shl 3) -bor ($blue5 -shr 2)
    }
}

function Convert-AdjustedRgb565 {
    param(
        [Parameter(Mandatory = $true)][int]$Value,
        [Parameter(Mandatory = $true)][int]$GainPercent,
        [Parameter(Mandatory = $true)][int]$LimitPercent
    )

    $rgb = Convert-FromRgb565 $Value
    $maximum = [Math]::Max($rgb.Red, [Math]::Max($rgb.Green, $rgb.Blue))
    $minimum = [Math]::Min($rgb.Red, [Math]::Min($rgb.Green, $rgb.Blue))
    $oldDelta = $maximum - $minimum
    if ($oldDelta -le 8 -or 0 -eq $maximum) {
        return $Value
    }

    $newDelta = [int][Math]::Floor((($oldDelta * $GainPercent) + 50) / 100)
    $quantizedLimit = $LimitPercent - 3
    $maximumDelta = [int][Math]::Floor((($maximum * $quantizedLimit) + 50) / 100)
    $newDelta = [Math]::Min($newDelta, $maximumDelta)
    $adjusted = foreach ($channel in @($rgb.Red, $rgb.Green, $rgb.Blue)) {
        $distance = $maximum - $channel
        $scaledDistance = [int][Math]::Floor(
            (($distance * $newDelta) + [int]($oldDelta / 2)) / $oldDelta)
        $maximum - $scaledDistance
    }
    return Convert-ToRgb565 $adjusted[0] $adjusted[1] $adjusted[2]
}

function Get-RgbSaturationPercent {
    param([Parameter(Mandatory = $true)][int]$Value)

    $rgb = Convert-FromRgb565 $Value
    $maximum = [Math]::Max($rgb.Red, [Math]::Max($rgb.Green, $rgb.Blue))
    $minimum = [Math]::Min($rgb.Red, [Math]::Min($rgb.Green, $rgb.Blue))
    if (0 -eq $maximum) {
        return 0.0
    }
    return (($maximum - $minimum) * 100.0) / $maximum
}

function Convert-ToPanelBytes {
    param([Parameter(Mandatory = $true)][int]$Rgb565)

    $red = ($Rgb565 -shr 11) -band 0x1F
    $green = ($Rgb565 -shr 5) -band 0x3F
    $blue = $Rgb565 -band 0x1F
    $bgr565 = ($blue -shl 11) -bor ($green -shl 5) -bor $red
    return [byte[]]@(
        (($bgr565 -shr 8) -band 0xFF),
        ($bgr565 -band 0xFF)
    )
}

function Assert-Equal {
    param(
        [Parameter(Mandatory = $true)]$Expected,
        [Parameter(Mandatory = $true)]$Actual,
        [Parameter(Mandatory = $true)][string]$Message
    )

    if ($Expected -ne $Actual) {
        throw "$Message expected=$Expected actual=$Actual"
    }
}

$fixedVectors = @(
    @{ Name = 'black'; R = 0; G = 0; B = 0; Rgb565 = 0x0000 },
    @{ Name = 'white'; R = 255; G = 255; B = 255; Rgb565 = 0xFFFF },
    @{ Name = 'red'; R = 255; G = 0; B = 0; Rgb565 = 0xF800 },
    @{ Name = 'green'; R = 0; G = 255; B = 0; Rgb565 = 0x07E0 },
    @{ Name = 'blue'; R = 0; G = 0; B = 255; Rgb565 = 0x001F },
    @{ Name = 'cyan'; R = 0; G = 255; B = 255; Rgb565 = 0x07FF },
    @{ Name = 'magenta'; R = 255; G = 0; B = 255; Rgb565 = 0xF81F },
    @{ Name = 'yellow'; R = 255; G = 255; B = 0; Rgb565 = 0xFFE0 }
)

foreach ($vector in $fixedVectors) {
    $actual = Convert-ToRgb565 -Red $vector.R -Green $vector.G -Blue $vector.B
    Assert-Equal -Expected $vector.Rgb565 -Actual $actual -Message $vector.Name
}

$sampleChannels = @(0, 1, 7, 8, 31, 63, 64, 127, 128, 191, 248, 254, 255)
$maxRedError = 0
$maxGreenError = 0
$maxBlueError = 0
foreach ($red in $sampleChannels) {
    foreach ($green in $sampleChannels) {
        foreach ($blue in $sampleChannels) {
            $decoded = Convert-FromRgb565 (Convert-ToRgb565 $red $green $blue)
            $maxRedError = [Math]::Max($maxRedError, [Math]::Abs($red - $decoded.Red))
            $maxGreenError = [Math]::Max($maxGreenError, [Math]::Abs($green - $decoded.Green))
            $maxBlueError = [Math]::Max($maxBlueError, [Math]::Abs($blue - $decoded.Blue))
        }
    }
}
if ($maxRedError -gt 7 -or $maxGreenError -gt 3 -or $maxBlueError -gt 7) {
    throw "RGB565 quantization error exceeded bound R=$maxRedError G=$maxGreenError B=$maxBlueError"
}

$expectedPanelBytes = @{
    red = '00-1F'
    green = '07-E0'
    blue = 'F8-00'
    cyan = 'FF-E0'
    magenta = 'F8-1F'
    yellow = '07-FF'
}
foreach ($vector in $fixedVectors | Where-Object { $expectedPanelBytes.ContainsKey($_.Name) }) {
    $actualBytes = (Convert-ToPanelBytes $vector.Rgb565 | ForEach-Object { '{0:X2}' -f $_ }) -join '-'
    Assert-Equal -Expected $expectedPanelBytes[$vector.Name] -Actual $actualBytes `
        -Message "$($vector.Name) BGR565 big-endian panel bytes"
}

$slimeSource = 0xD34F
$slimeAdjusted = Convert-AdjustedRgb565 $slimeSource 160 85
$slimeSourceSaturation = Get-RgbSaturationPercent $slimeSource
$slimeAdjustedSaturation = Get-RgbSaturationPercent $slimeAdjusted
Assert-Equal -Expected 0xD148 -Actual $slimeAdjusted `
    -Message 'slime representative color compensation'
if ($slimeAdjustedSaturation -le $slimeSourceSaturation -or
    $slimeAdjustedSaturation -ge 85.5) {
    throw ('Slime saturation outside expected middle range: source={0:F1}% adjusted={1:F1}%' -f `
        $slimeSourceSaturation, $slimeAdjustedSaturation)
}

$neutralGray = 0x8410
Assert-Equal -Expected $neutralGray `
    -Actual (Convert-AdjustedRgb565 $neutralGray 160 85) `
    -Message 'near-gray compensation bypass'
$redAdjusted = Convert-AdjustedRgb565 0xF800 160 85
if ((Get-RgbSaturationPercent $redAdjusted) -gt 86.0) {
    throw 'Full red exceeded configured saturation limit'
}

$assetSource = Join-Path $PSScriptRoot '..\components\companion_expression\generated\companion_expression_assets.c'
$assetText = Get-Content -LiteralPath $assetSource -Raw
$packNames = @(
    'icebox', 'crimson_slime', 'jade_frog', 'bone_skull',
    'cobalt_owl', 'magenta_octopus', 'silver_husky', 'amber_duck'
)
$paletteSummary = @()
foreach ($packName in $packNames) {
    $pattern = "static const uint16_t s_${packName}_palette\[\] = \{(?<values>.*?)\};"
    $match = [regex]::Match($assetText, $pattern,
        [Text.RegularExpressions.RegexOptions]::Singleline)
    if (-not $match.Success) {
        throw "Palette not found: $packName"
    }
    $values = [regex]::Matches($match.Groups['values'].Value, '0x[0-9A-Fa-f]{4}') |
        ForEach-Object { [Convert]::ToInt32($_.Value.Substring(2), 16) }
    if ($values.Count -lt 2 -or $values.Count -gt 16) {
        throw "Palette size outside 2..16: $packName count=$($values.Count)"
    }
    foreach ($value in $values) {
        $rgb = Convert-FromRgb565 $value
        $roundTrip = Convert-ToRgb565 $rgb.Red $rgb.Green $rgb.Blue
        Assert-Equal -Expected $value -Actual $roundTrip `
            -Message "$packName palette RGB565 round trip"
    }
    $paletteSummary += "$packName=$($values.Count)"
}

Write-Host ('PASS RGB565 fixed vectors, quantization max R/G/B={0}/{1}/{2}' -f `
    $maxRedError, $maxGreenError, $maxBlueError)
Write-Host 'PASS current flush emits BGR565 big-endian bytes for RGB/CMY'
Write-Host ('PASS expression saturation: slime {0:F1}% -> {1:F1}%, gray preserved, cap enforced' -f `
    $slimeSourceSaturation, $slimeAdjustedSaturation)
Write-Host ('PASS generated palettes: ' + ($paletteSummary -join ', '))
