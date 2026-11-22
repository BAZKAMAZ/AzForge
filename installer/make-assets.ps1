param(
    [string]$RepoRoot = (Split-Path -Parent $PSScriptRoot)
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$assetsDir = Join-Path $PSScriptRoot 'assets'
New-Item -ItemType Directory -Force -Path $assetsDir | Out-Null

# Polygon coordinates from res/AFhammer.svg (viewBox 0 0 128 128)
$hammerPts = @(
    @(38.0, 108.5), @(34.0, 108.5), @(29.0, 105.5), @(26.5, 103.0), @(26.5, 99.0),
    @(38.5, 84.0),  @(51.5, 64.0),  @(64.5, 50.0),  @(73.5, 37.0),  @(76.5, 30.0),
    @(76.5, 28.0),  @(71.0, 23.5),  @(58.0, 24.5),  @(57.5, 22.0),  @(65.0, 17.5),
    @(72.0, 15.5),  @(82.0, 16.5),  @(91.0, 20.5),  @(98.5, 26.0),  @(98.5, 29.0),
    @(101.0, 31.5), @(104.0, 31.5), @(109.0, 34.5), @(109.5, 38.0), @(103.0, 46.5),
    @(98.0, 44.5),  @(93.0, 38.5),  @(91.0, 37.5),  @(87.0, 38.5),  @(72.5, 55.0),
    @(63.5, 72.0)
)

function New-RoundedRectPath([float]$x, [float]$y, [float]$w, [float]$h, [float]$r) {
    $p = New-Object System.Drawing.Drawing2D.GraphicsPath
    $d = $r * 2
    $p.AddArc($x, $y, $d, $d, 180, 90)
    $p.AddArc($x + $w - $d, $y, $d, $d, 270, 90)
    $p.AddArc($x + $w - $d, $y + $h - $d, $d, $d, 0, 90)
    $p.AddArc($x, $y + $h - $d, $d, $d, 90, 90)
    $p.CloseFigure()
    return $p
}

$icoSizes = @(16, 32, 48, 64, 256)
$pngBlobs = @()

foreach ($sz in $icoSizes) {
    $bmp = New-Object System.Drawing.Bitmap($sz, $sz, [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $pad = [Math]::Max(1.0, $sz * 0.05)
    $rad = [Math]::Max(2.0, $sz * 0.22)
    $rectPath = New-RoundedRectPath $pad $pad ($sz - $pad * 2) ($sz - $pad * 2) $rad

    $bgBrush = New-Object System.Drawing.Drawing2D.LinearGradientBrush(
        (New-Object System.Drawing.PointF(0, 0)),
        (New-Object System.Drawing.PointF($sz, $sz)),
        [System.Drawing.Color]::FromArgb(255, 22, 27, 36),
        [System.Drawing.Color]::FromArgb(255, 11, 14, 20)
    )
    $g.FillPath($bgBrush, $rectPath)
    $bgBrush.Dispose()

    $borderW = [Math]::Max(1.0, $sz * 0.045)
    $borderPen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(190, 255, 120, 0), $borderW)
    $g.DrawPath($borderPen, $rectPath)
    $borderPen.Dispose()
    $rectPath.Dispose()

    $hammerSize = $sz * 0.84
    $offset = ($sz - $hammerSize) / 2.0
    $scale = $hammerSize / 128.0
    $pts = New-Object 'System.Drawing.PointF[]' $hammerPts.Count
    for ($i = 0; $i -lt $hammerPts.Count; $i++) {
        $pts[$i] = New-Object System.Drawing.PointF(
            [float]($offset + $hammerPts[$i][0] * $scale),
            [float]($offset + $hammerPts[$i][1] * $scale)
        )
    }

    $fillBrush = New-Object System.Drawing.SolidBrush([System.Drawing.Color]::FromArgb(42, 255, 120, 0))
    $g.FillPolygon($fillBrush, $pts)
    $fillBrush.Dispose()

    $pen = New-Object System.Drawing.Pen([System.Drawing.Color]::FromArgb(255, 255, 120, 0), [Math]::Max(1.4, $sz * 0.055))
    $pen.LineJoin = [System.Drawing.Drawing2D.LineJoin]::Round
    $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
    $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
    $g.DrawPolygon($pen, $pts)
    $pen.Dispose()

    $ms = New-Object System.IO.MemoryStream
    $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
    $pngBlobs += ,($ms.ToArray())
    $ms.Dispose()
    $g.Dispose()
    $bmp.Dispose()
}

$icoPath = Join-Path $assetsDir 'AzForge.ico'
$fs = [System.IO.File]::Create($icoPath)
$bw = New-Object System.IO.BinaryWriter($fs)
$bw.Write([UInt16]0)
$bw.Write([UInt16]1)
$bw.Write([UInt16]$icoSizes.Count)

$dataOffset = 6 + 16 * $icoSizes.Count
for ($i = 0; $i -lt $icoSizes.Count; $i++) {
    $sz = $icoSizes[$i]
    $blob = $pngBlobs[$i]
    $b = if ($sz -ge 256) { [byte]0 } else { [byte]$sz }
    $bw.Write($b)
    $bw.Write($b)
    $bw.Write([byte]0)
    $bw.Write([byte]0)
    $bw.Write([UInt16]1)
    $bw.Write([UInt16]32)
    $bw.Write([UInt32]$blob.Length)
    $bw.Write([UInt32]$dataOffset)
    $dataOffset += $blob.Length
}
for ($i = 0; $i -lt $pngBlobs.Count; $i++) {
    $bw.Write($pngBlobs[$i])
}
$bw.Dispose()
$fs.Dispose()

Write-Host "Generated: $icoPath"
