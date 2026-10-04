# Draws the Free Camera Esc-menu icon in the style of the game's menu icons (menu_options_*.dds): two flat colors,
# cream background with a dark glyph, inverted for hover. Writes 64x64 uncompressed 24-bit DDS files (the same format
# as the stock icons) into mod/textures/gui, plus PNG previews if -PreviewDir is given.
param([string]$PreviewDir)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$cream = [Drawing.Color]::FromArgb(0xf0, 0xe3, 0xcc)
$dark = [Drawing.Color]::FromArgb(0x40, 0x39, 0x2b)
$size = 64
$scale = 4 # drawn at 256x256 and scaled down, for smooth edges like the stock icons
$outDir = Join-Path $PSScriptRoot '..\mod\textures\gui'

function Draw-Icon($background, $glyph) {
    $s = $size * $scale
    $big = New-Object Drawing.Bitmap $s, $s
    $g = [Drawing.Graphics]::FromImage($big)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear($background)
    $fg = New-Object Drawing.SolidBrush $glyph
    $bg = New-Object Drawing.SolidBrush $background
    function Px($v) { [single]($v * $scale) } # coordinates below are in 64x64 icon pixels

    # camera body: rounded rectangle
    $body = New-Object Drawing.Drawing2D.GraphicsPath
    $x = 7; $y = 20; $w = 50; $h = 33; $r = 7
    $body.AddArc((Px $x), (Px $y), (Px ($r * 2)), (Px ($r * 2)), 180, 90)
    $body.AddArc((Px ($x + $w - $r * 2)), (Px $y), (Px ($r * 2)), (Px ($r * 2)), 270, 90)
    $body.AddArc((Px ($x + $w - $r * 2)), (Px ($y + $h - $r * 2)), (Px ($r * 2)), (Px ($r * 2)), 0, 90)
    $body.AddArc((Px $x), (Px ($y + $h - $r * 2)), (Px ($r * 2)), (Px ($r * 2)), 90, 90)
    $body.CloseFigure()
    $g.FillPath($fg, $body)

    # viewfinder hump on top
    $hump = New-Object Drawing.Drawing2D.GraphicsPath
    $hump.AddPolygon([Drawing.PointF[]]@(
        (New-Object Drawing.PointF (Px 21), (Px 21)), (New-Object Drawing.PointF (Px 25), (Px 12)),
        (New-Object Drawing.PointF (Px 39), (Px 12)), (New-Object Drawing.PointF (Px 43), (Px 21))))
    $g.FillPath($fg, $hump)

    # lens: ring cut out of the body, with a solid center
    $g.FillEllipse($bg, (Px 20), (Px 24), (Px 24), (Px 24))
    $g.FillEllipse($fg, (Px 25), (Px 29), (Px 14), (Px 14))

    # flash dot
    $g.FillEllipse($bg, (Px 47), (Px 25), (Px 5), (Px 5))

    $small = New-Object Drawing.Bitmap $size, $size
    $gs = [Drawing.Graphics]::FromImage($small)
    $gs.InterpolationMode = 'HighQualityBicubic'
    $gs.PixelOffsetMode = 'HighQuality'
    $gs.DrawImage($big, 0, 0, $size, $size)
    $g.Dispose(); $gs.Dispose(); $big.Dispose()
    return $small
}

# Uncompressed 24-bit RGB DDS, top-down rows, BGR byte order (masks R=0xFF0000 G=0xFF00 B=0xFF), no mipmaps.
function Save-Dds($bitmap, $path) {
    $w = $bitmap.Width; $h = $bitmap.Height
    $ms = New-Object IO.MemoryStream
    $bw = New-Object IO.BinaryWriter $ms
    $bw.Write([Text.Encoding]::ASCII.GetBytes('DDS '))
    $bw.Write([uint32]124)                  # header size
    $bw.Write([uint32]0x100F)               # CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
    $bw.Write([uint32]$h); $bw.Write([uint32]$w)
    $bw.Write([uint32]($w * 3))             # pitch
    $bw.Write([uint32]0); $bw.Write([uint32]0) # depth, mipmap count
    for ($i = 0; $i -lt 11; $i++) { $bw.Write([uint32]0) } # reserved
    $bw.Write([uint32]32); $bw.Write([uint32]0x40) # pixel format: size, DDPF_RGB
    $bw.Write([uint32]0); $bw.Write([uint32]24)    # fourcc, bit count
    $bw.Write([uint32]0x00FF0000); $bw.Write([uint32]0x0000FF00); $bw.Write([uint32]0x000000FF); $bw.Write([uint32]0)
    $bw.Write([uint32]0x1000)               # caps: texture
    for ($i = 0; $i -lt 4; $i++) { $bw.Write([uint32]0) }
    for ($y = 0; $y -lt $h; $y++) {
        for ($x = 0; $x -lt $w; $x++) {
            $c = $bitmap.GetPixel($x, $y)
            $bw.Write([byte]$c.B); $bw.Write([byte]$c.G); $bw.Write([byte]$c.R)
        }
    }
    $bw.Flush()
    [IO.File]::WriteAllBytes($path, $ms.ToArray())
}

New-Item -ItemType Directory -Force $outDir | Out-Null
$normal = Draw-Icon $cream $dark
$hover = Draw-Icon $dark $cream
Save-Dds $normal (Join-Path $outDir 'dk2ml_freecam_normal.dds')
Save-Dds $hover (Join-Path $outDir 'dk2ml_freecam_hover.dds')
Write-Host "Wrote $outDir\dk2ml_freecam_normal.dds and dk2ml_freecam_hover.dds"

if ($PreviewDir) {
    $sheet = New-Object Drawing.Bitmap 520, 256
    $g = [Drawing.Graphics]::FromImage($sheet)
    $g.InterpolationMode = 'NearestNeighbor'
    $g.DrawImage($normal, 0, 0, 256, 256)
    $g.DrawImage($hover, 264, 0, 256, 256)
    $sheet.Save((Join-Path $PreviewDir 'freecam_icon_preview.png'))
    $g.Dispose()
}
