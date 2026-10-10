# The HUD button (stock HUD toggle style) and the camera wheel (action wheel style) into mod/textures/hud, as
# uncompressed 32-bit BGRA DDS like the stock textures. -PreviewDir also writes a PNG sheet.
param([string]$PreviewDir)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$cream = [Drawing.Color]::FromArgb(0xf0, 0xe3, 0xcc)
$dark = [Drawing.Color]::FromArgb(0x21, 0x1e, 0x1d)
$sliceDark = [Drawing.Color]::FromArgb(0x30, 0x20, 0x1a)
$gold = [Drawing.Color]::FromArgb(0xff, 0xc0, 0x00)
$glowCenter = [Drawing.Color]::FromArgb(0x33, 0x1b, 0x08)
$glowEdge = [Drawing.Color]::FromArgb(0xc8, 0x5e, 0x04)
$orangeGlyph = [Drawing.Color]::FromArgb(0xd0, 0x62, 0x00)
$scale = 4 # drawn at 4x and scaled down, for smooth edges like the stock textures
$outDir = Join-Path $PSScriptRoot '..\mod\textures\hud'

# Lock toggle geometry, in its 104x104 pixels
$toggleSize = 104
$discRadius = 30
$rimWidth = 2.5
$segmentInner = 32.5
$segmentOuter = 37
$segmentGap = 20 # degrees, centered on each axis
$shadowRadius = 43

# Wheel geometry
$sliceSize = 120
$sliceRadius = 42
$wheelRadius = 230 # the first row's slice centers, in GUI canvas pixels
$ringRadii = 224, 235
$ringWidth = 3
$ringSpan = 15, 165 # degrees above the horizontal
$ringTexW = 488
$ringTexH = 204
$ringCenterBelow = 40 # the wheel's center sits this far below the ring texture's bottom edge

# Every drawing call below takes coordinates in output pixels. Px scales them to the 4x canvas.
function Px($v) { [single]($v * $scale) }
function Pt($x, $y) { New-Object Drawing.PointF (Px $x), (Px $y) }

function Circle($cx, $cy, $r) {
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $path.AddEllipse((Px ($cx - $r)), (Px ($cy - $r)), (Px (2 * $r)), (Px (2 * $r)))
    return $path
}

function New-Canvas($w, $h) {
    $bitmap = New-Object Drawing.Bitmap (Px $w), (Px $h), ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [Drawing.Graphics]::FromImage($bitmap)
    $g.SmoothingMode = 'AntiAlias'
    $g.Clear([Drawing.Color]::FromArgb(0, 5, 5, 5))
    return @{ Bitmap = $bitmap; Graphics = $g; W = $w; H = $h }
}

function Finish-Canvas($canvas) {
    $small = New-Object Drawing.Bitmap $canvas.W, $canvas.H, ([Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $gs = [Drawing.Graphics]::FromImage($small)
    $gs.CompositingMode = 'SourceCopy'
    $gs.InterpolationMode = 'HighQualityBicubic'
    $gs.PixelOffsetMode = 'HighQuality'
    $gs.DrawImage($canvas.Bitmap, 0, 0, $canvas.W, $canvas.H)
    $canvas.Graphics.Dispose(); $gs.Dispose(); $canvas.Bitmap.Dispose()
    return $small
}

function Glyph-Pen($color, $k) {
    $pen = New-Object Drawing.Pen $color, (Px (5 * $k))
    $pen.LineJoin = 'Round'
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
    return $pen
}

# --- glyphs: drawn around ($cx, $cy), sized for the toggle's disc at $k = 1 ---

# the map seen from straight above (a rounded frame) with a chevron looking down onto its center
function Glyph-Lock($g, $cx, $cy, $k, $color) {
    $pen = Glyph-Pen $color $k
    $side = 36 * $k; $r = 6 * $k
    $x = $cx - $side / 2; $y = $cy - $side / 2
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $path.AddArc((Px $x), (Px $y), (Px (2 * $r)), (Px (2 * $r)), 180, 90)
    $path.AddArc((Px ($x + $side - 2 * $r)), (Px $y), (Px (2 * $r)), (Px (2 * $r)), 270, 90)
    $path.AddArc((Px ($x + $side - 2 * $r)), (Px ($y + $side - 2 * $r)), (Px (2 * $r)), (Px (2 * $r)), 0, 90)
    $path.AddArc((Px $x), (Px ($y + $side - 2 * $r)), (Px (2 * $r)), (Px (2 * $r)), 90, 90)
    $path.CloseFigure()
    $g.DrawPath($pen, $path)
    $g.DrawLines($pen, [Drawing.PointF[]]@(
        (Pt ($cx - 9 * $k) ($cy - 10 * $k)), (Pt $cx ($cy - 1 * $k)), (Pt ($cx + 9 * $k) ($cy - 10 * $k))))
    $brush = New-Object Drawing.SolidBrush $color
    $g.FillEllipse($brush, (Px ($cx - 4 * $k)), (Px ($cy + 4.5 * $k)), (Px (8 * $k)), (Px (8 * $k)))
}

# a three-quarter circle with an arrowhead; $dir = 1 turns clockwise (right), -1 counterclockwise (left)
function Glyph-Rotate($g, $cx, $cy, $k, $color, $dir) {
    $pen = Glyph-Pen $color $k
    $r = 13 * $k
    # Screen angles (y down): the gap is at the top, and the arrow ends at its left (clockwise) or right side.
    $start = if ($dir -gt 0) { -60 } else { 240 }
    $sweep = 270 * $dir
    $g.DrawArc($pen, (Px ($cx - $r)), (Px ($cy - $r)), (Px (2 * $r)), (Px (2 * $r)), $start, $sweep)

    $end = ($start + $sweep) * [Math]::PI / 180
    $ex = $cx + $r * [Math]::Cos($end); $ey = $cy + $r * [Math]::Sin($end)
    $tx = -[Math]::Sin($end) * $dir; $ty = [Math]::Cos($end) * $dir # direction of travel at the end
    $nx = -$ty; $ny = $tx
    $head = 8 * $k; $half = 7 * $k
    $brush = New-Object Drawing.SolidBrush $color
    $g.FillPolygon($brush, [Drawing.PointF[]]@(
        (Pt ($ex + $tx * $head) ($ey + $ty * $head)),
        (Pt ($ex + $nx * $half - $tx * 2 * $k) ($ey + $ny * $half - $ty * 2 * $k)),
        (Pt ($ex - $nx * $half - $tx * 2 * $k) ($ey - $ny * $half - $ty * 2 * $k))))
}

# reset: two arrows chasing each other around a circle (the usual "refresh" sign), unlike the single rotate arrows
function Glyph-Reset($g, $cx, $cy, $k, $color) {
    $pen = Glyph-Pen $color $k
    $brush = New-Object Drawing.SolidBrush $color
    $r = 13 * $k; $sweep = 140; $head = 7 * $k; $half = 6 * $k
    foreach ($start in -165, 15) {
        $g.DrawArc($pen, (Px ($cx - $r)), (Px ($cy - $r)), (Px (2 * $r)), (Px (2 * $r)), $start, $sweep)

        # clockwise on screen (y down): the direction of travel at the end
        $end = ($start + $sweep) * [Math]::PI / 180
        $ex = $cx + $r * [Math]::Cos($end); $ey = $cy + $r * [Math]::Sin($end)
        $tx = -[Math]::Sin($end); $ty = [Math]::Cos($end)
        $nx = -$ty; $ny = $tx
        $g.FillPolygon($brush, [Drawing.PointF[]]@(
            (Pt ($ex + $tx * $head) ($ey + $ty * $head)),
            (Pt ($ex + $nx * $half - $tx * 2 * $k) ($ey + $ny * $half - $ty * 2 * $k)),
            (Pt ($ex - $nx * $half - $tx * 2 * $k) ($ey - $ny * $half - $ty * 2 * $k))))
    }
}

# a compass needle: the north half solid, the south half outlined
function Glyph-North($g, $cx, $cy, $k, $color) {
    $brush = New-Object Drawing.SolidBrush $color
    $pen = New-Object Drawing.Pen $color, (Px (3 * $k))
    $pen.LineJoin = 'Round'
    $w = 8 * $k; $h = 19 * $k
    $g.FillPolygon($brush, [Drawing.PointF[]]@((Pt $cx ($cy - $h)), (Pt ($cx + $w) $cy), (Pt ($cx - $w) $cy)))
    $g.DrawPolygon($pen, [Drawing.PointF[]]@((Pt $cx ($cy + $h)), (Pt ($cx + $w) $cy), (Pt ($cx - $w) $cy)))
}

# an arrow up ($dir = 1) or down (-1) from a horizon line
function Glyph-Tilt($g, $cx, $cy, $k, $color, $dir) {
    $pen = Glyph-Pen $color $k
    $horizonY = $cy + 10 * $k * $dir
    $g.DrawLine($pen, (Pt ($cx - 15 * $k) $horizonY), (Pt ($cx + 15 * $k) $horizonY))
    $tipY = $cy - 15 * $k * $dir
    $g.DrawLine($pen, (Pt $cx ($horizonY - 6 * $k * $dir)), (Pt $cx $tipY))
    $g.DrawLines($pen, [Drawing.PointF[]]@(
        (Pt ($cx - 8 * $k) ($tipY + 8 * $k * $dir)), (Pt $cx $tipY), (Pt ($cx + 8 * $k) ($tipY + 8 * $k * $dir))))
}

# a padlock: a body with a shackle over it. Open, the shackle is lifted and its right leg doesn't reach the body.
function Glyph-Padlock($g, $cx, $cy, $k, $color, [bool]$closed) {
    $brush = New-Object Drawing.SolidBrush $color
    $pen = New-Object Drawing.Pen $color, (Px (3 * $k))
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
    $bodyTop = $cy - 1 * $k
    $g.FillRectangle($brush, (Px ($cx - 7 * $k)), (Px $bodyTop), (Px (14 * $k)), (Px (11 * $k)))

    $r = 4.5 * $k
    $lift = if ($closed) { 0 } else { 4 * $k }
    $arcY = $cy - 6 * $k - $lift
    $rightEnd = if ($closed) { $bodyTop } else { $arcY + 2 * $k }
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $path.AddLine((Pt ($cx - $r) $bodyTop), (Pt ($cx - $r) $arcY))
    $path.AddArc((Px ($cx - $r)), (Px ($arcY - $r)), (Px (2 * $r)), (Px (2 * $r)), 180, 180)
    $path.AddLine((Pt ($cx + $r) $arcY), (Pt ($cx + $r) $rightEnd))
    $g.DrawPath($pen, $path)
}

# lock rotation: a small rotate arrow and a padlock
function Glyph-LockRotation($g, $cx, $cy, $k, $color, [bool]$closed) {
    Glyph-Rotate $g ($cx - 9 * $k) ($cy + 1 * $k) ($k * 0.62) $color 1
    Glyph-Padlock $g ($cx + 10 * $k) $cy $k $color $closed
}

# lock tilt: a small up/down arrow and a padlock
function Glyph-LockTilt($g, $cx, $cy, $k, $color, [bool]$closed) {
    $pen = Glyph-Pen $color ($k * 0.65)
    $x = $cx - 9 * $k; $top = $cy - 11 * $k; $bottom = $cy + 11 * $k; $head = 5 * $k
    $g.DrawLine($pen, (Pt $x $top), (Pt $x $bottom))
    $g.DrawLines($pen, [Drawing.PointF[]]@((Pt ($x - $head) ($top + $head)), (Pt $x $top), (Pt ($x + $head) ($top + $head))))
    $g.DrawLines($pen, [Drawing.PointF[]]@(
        (Pt ($x - $head) ($bottom - $head)), (Pt $x $bottom), (Pt ($x + $head) ($bottom - $head))))
    Glyph-Padlock $g ($cx + 10 * $k) $cy $k $color $closed
}

# --- the lock toggle ---

# One arc segment of the toggle's outer ring, between two radii.
function Segment($c, $startDeg, $sweepDeg) {
    $path = New-Object Drawing.Drawing2D.GraphicsPath
    $o = $segmentOuter; $i = $segmentInner
    $path.AddArc((Px ($c - $o)), (Px ($c - $o)), (Px (2 * $o)), (Px (2 * $o)), $startDeg, $sweepDeg)
    $path.AddArc((Px ($c - $i)), (Px ($c - $i)), (Px (2 * $i)), (Px (2 * $i)), $startDeg + $sweepDeg, -$sweepDeg)
    $path.CloseFigure()
    return $path
}

# $hover: cream disc with a dark glyph. $on: the orange glow (normal) or an orange glyph (hover).
function Draw-Toggle([bool]$on, [bool]$hover) {
    $canvas = New-Canvas $toggleSize $toggleSize
    $g = $canvas.Graphics
    $c = $toggleSize / 2

    # soft shadow around the ring, fading out toward $shadowRadius
    $shadow = New-Object Drawing.Drawing2D.PathGradientBrush (Circle $c $c $shadowRadius)
    $blend = New-Object Drawing.Drawing2D.ColorBlend 4
    $blend.Colors = [Drawing.Color[]]@(
        [Drawing.Color]::FromArgb(0, 5, 5, 5), [Drawing.Color]::FromArgb(0x40, 4, 4, 4),
        [Drawing.Color]::FromArgb(0x90, 3, 3, 3), [Drawing.Color]::FromArgb(0x90, 3, 3, 3))
    $blend.Positions = [single[]]@(0, (1 - 39 / $shadowRadius), (1 - 35 / $shadowRadius), 1)
    $shadow.InterpolationColors = $blend
    $g.FillPath($shadow, (Circle $c $c $shadowRadius))

    # outer ring: four outlined segments with gaps on the axes
    $creamBrush = New-Object Drawing.SolidBrush $cream
    $darkBrush = New-Object Drawing.SolidBrush $dark
    $outline = New-Object Drawing.Pen $cream, (Px 1.6)
    foreach ($axis in 0, 90, 180, 270) {
        $seg = Segment $c ($axis + $segmentGap / 2) (90 - $segmentGap)
        $g.FillPath($darkBrush, $seg)
        $g.DrawPath($outline, $seg)
    }

    # disc: cream rim, then the interior
    $g.FillPath($creamBrush, (Circle $c $c $discRadius))
    $inner = Circle $c $c ($discRadius - $rimWidth)
    if ($hover) {
        $g.FillPath($creamBrush, $inner)
    } elseif ($on) {
        $glow = New-Object Drawing.Drawing2D.PathGradientBrush $inner
        $glow.CenterColor = $glowCenter
        $glow.SurroundColors = [Drawing.Color[]]@($glowEdge)
        $g.FillPath($darkBrush, $inner)
        $g.FillPath($glow, $inner)
    } else {
        $g.FillPath($darkBrush, $inner)
    }

    $glyphColor = $cream
    if ($hover) {
        $glyphColor = if ($on) { $orangeGlyph } else { $dark }
    }
    Glyph-Lock $g $c $c 1 $glyphColor
    return Finish-Canvas $canvas
}

# --- the wheel ---

# One slice: $glyph is a scriptblock drawing around the slice's center, called with $glyphArg last. $hover adds the gold
# ring. $on fills the disc with the HUD toggle's orange glow (a lock toggle that is on).
function Draw-Slice($glyph, [bool]$hover, [bool]$on = $false, $glyphArg = $null) {
    $canvas = New-Canvas $sliceSize $sliceSize
    $g = $canvas.Graphics
    $c = $sliceSize / 2

    # soft shadow just outside the disc
    $shadow = New-Object Drawing.Drawing2D.PathGradientBrush (Circle $c $c ($sliceRadius + 7))
    $blend = New-Object Drawing.Drawing2D.ColorBlend 3
    $blend.Colors = [Drawing.Color[]]@(
        [Drawing.Color]::FromArgb(0, 5, 5, 5), [Drawing.Color]::FromArgb(0x80, 3, 3, 3), [Drawing.Color]::FromArgb(0x80, 3, 3, 3))
    $blend.Positions = [single[]]@(0, (7 / ($sliceRadius + 7)), 1)
    $shadow.InterpolationColors = $blend
    $g.FillPath($shadow, (Circle $c $c ($sliceRadius + 7)))

    $g.FillPath((New-Object Drawing.SolidBrush $sliceDark), (Circle $c $c $sliceRadius))
    if ($on) {
        $glow = New-Object Drawing.Drawing2D.PathGradientBrush (Circle $c $c $sliceRadius)
        $glow.CenterColor = $glowCenter
        $glow.SurroundColors = [Drawing.Color[]]@($glowEdge)
        $g.FillPath($glow, (Circle $c $c $sliceRadius))
    }
    $g.DrawPath((New-Object Drawing.Pen $cream, (Px 2.5)), (Circle $c $c ($sliceRadius - 1.5)))
    & $glyph $g $c $c 1.25 $cream $glyphArg

    if ($hover) {
        # a glow fading out on both sides of a solid gold ring, like general_actions_hover
        for ($i = 6; $i -ge 1; $i--) {
            $alpha = [int](0x50 * (1 - $i / 7))
            $glowPen = New-Object Drawing.Pen ([Drawing.Color]::FromArgb($alpha, $gold)), (Px (3 + 2 * $i))
            $g.DrawPath($glowPen, (Circle $c $c ($sliceRadius + 3)))
        }
        $g.DrawPath((New-Object Drawing.Pen $gold, (Px 3.5)), (Circle $c $c ($sliceRadius + 3)))
    }
    return Finish-Canvas $canvas
}

# The backdrop: two thin cream arcs through the first row's circle, over the upper half. The texture's center is 142
# above the wheel's center (gui/freecam_hud.xml places it there).
function Draw-Ring() {
    $canvas = New-Canvas $ringTexW $ringTexH
    $g = $canvas.Graphics
    $cx = $ringTexW / 2; $cy = $ringTexH + $ringCenterBelow
    $pen = New-Object Drawing.Pen ([Drawing.Color]::FromArgb(0xd0, $cream)), (Px $ringWidth)
    $pen.StartCap = 'Round'; $pen.EndCap = 'Round'
    foreach ($r in $ringRadii) {
        # screen angles (y down): the arc runs over the top, from 165 to 15 degrees above the horizontal
        $g.DrawArc($pen, (Px ($cx - $r)), (Px ($cy - $r)), (Px (2 * $r)), (Px (2 * $r)), -$ringSpan[1], $ringSpan[1] - $ringSpan[0])
    }
    return Finish-Canvas $canvas
}

# Uncompressed 32-bit BGRA DDS, straight alpha, bottom-up rows (the game shows the first row at the bottom), masks
# R=0xFF0000 G=0xFF00 B=0xFF A=0xFF000000, no mipmaps.
function Save-Dds($bitmap, $path) {
    $w = $bitmap.Width; $h = $bitmap.Height
    $ms = New-Object IO.MemoryStream
    $bw = New-Object IO.BinaryWriter $ms
    $bw.Write([Text.Encoding]::ASCII.GetBytes('DDS '))
    $bw.Write([uint32]124)                  # header size
    $bw.Write([uint32]0x100F)               # CAPS | HEIGHT | WIDTH | PITCH | PIXELFORMAT
    $bw.Write([uint32]$h); $bw.Write([uint32]$w)
    $bw.Write([uint32]($w * 4))             # pitch
    $bw.Write([uint32]0); $bw.Write([uint32]0) # depth, mipmap count
    for ($i = 0; $i -lt 11; $i++) { $bw.Write([uint32]0) } # reserved
    $bw.Write([uint32]32); $bw.Write([uint32]0x41) # pixel format: size, DDPF_RGB | DDPF_ALPHAPIXELS
    $bw.Write([uint32]0); $bw.Write([uint32]32)    # fourcc, bit count
    $bw.Write([uint32]0x00FF0000); $bw.Write([uint32]0x0000FF00); $bw.Write([uint32]0x000000FF)
    $bw.Write([uint32]4278190080)           # alpha mask 0xFF000000 (a hex literal would be a negative int)
    $bw.Write([uint32]0x1000)               # caps: texture
    for ($i = 0; $i -lt 4; $i++) { $bw.Write([uint32]0) }
    for ($y = $h - 1; $y -ge 0; $y--) {
        for ($x = 0; $x -lt $w; $x++) {
            $c = $bitmap.GetPixel($x, $y)
            $bw.Write([byte]$c.B); $bw.Write([byte]$c.G); $bw.Write([byte]$c.R); $bw.Write([byte]$c.A)
        }
    }
    $bw.Flush()
    [IO.File]::WriteAllBytes($path, $ms.ToArray())
}

$slices = [ordered]@{
    'lock'         = { param($g, $x, $y, $k, $color) Glyph-Lock $g $x $y $k $color }
    'reset'        = { param($g, $x, $y, $k, $color) Glyph-Reset $g $x $y $k $color }
    'rotate_left'  = { param($g, $x, $y, $k, $color) Glyph-Rotate $g $x $y $k $color -1 }
    'rotate_right' = { param($g, $x, $y, $k, $color) Glyph-Rotate $g $x $y $k $color 1 }
    'north'        = { param($g, $x, $y, $k, $color) Glyph-North $g $x $y $k $color }
    'tilt_up'      = { param($g, $x, $y, $k, $color) Glyph-Tilt $g $x $y $k $color 1 }
    'tilt_down'    = { param($g, $x, $y, $k, $color) Glyph-Tilt $g $x $y $k $color -1 }
}

$images = [ordered]@{
    'dk2ml_freecam_lock_off_normal' = Draw-Toggle $false $false
    'dk2ml_freecam_lock_off_hover'  = Draw-Toggle $false $true
    'dk2ml_freecam_lock_on_normal'  = Draw-Toggle $true $false
    'dk2ml_freecam_lock_on_hover'   = Draw-Toggle $true $true
}
foreach ($name in $slices.Keys) {
    $images["dk2ml_freecam_wheel_${name}_normal"] = Draw-Slice $slices[$name] $false
    $images["dk2ml_freecam_wheel_${name}_hover"] = Draw-Slice $slices[$name] $true
}

# the outer row's toggles, off (open padlock) and on (closed padlock, orange glow); the last argument is "closed"
$toggles = [ordered]@{
    'lockrot'  = { param($g, $x, $y, $k, $color, $closed) Glyph-LockRotation $g $x $y $k $color $closed }
    'locktilt' = { param($g, $x, $y, $k, $color, $closed) Glyph-LockTilt $g $x $y $k $color $closed }
}
foreach ($name in $toggles.Keys) {
    foreach ($on in $false, $true) {
        $state = if ($on) { 'on' } else { 'off' }
        $images["dk2ml_freecam_wheel_${name}_${state}_normal"] = Draw-Slice $toggles[$name] $false $on $on
        $images["dk2ml_freecam_wheel_${name}_${state}_hover"] = Draw-Slice $toggles[$name] $true $on $on
    }
}
$images['dk2ml_freecam_wheel_ring'] = Draw-Ring

New-Item -ItemType Directory -Force $outDir | Out-Null
foreach ($name in $images.Keys) {
    Save-Dds $images[$name] (Join-Path $outDir "$name.dds")
}
Write-Host "Wrote $($images.Count) textures to $outDir"

if ($PreviewDir) {
    # the images side by side at their own size, on a mid-grey backdrop, wrapped into rows
    $perRow = 8
    $cell = 128
    $rows = [Math]::Ceiling(($images.Count - 1) / $perRow)
    $sheet = New-Object Drawing.Bitmap ($cell * $perRow), ($cell * $rows + $ringTexH + 8)
    $g = [Drawing.Graphics]::FromImage($sheet)
    $g.Clear([Drawing.Color]::FromArgb(0x5a, 0x5a, 0x5a))
    $i = 0
    foreach ($name in $images.Keys) {
        $img = $images[$name]
        if ($name -eq 'dk2ml_freecam_wheel_ring') {
            $g.DrawImage($img, 0, $cell * $rows + 4, $img.Width, $img.Height)
            continue
        }
        $x = ($i % $perRow) * $cell + ($cell - $img.Width) / 2
        $y = [Math]::Floor($i / $perRow) * $cell + ($cell - $img.Height) / 2
        $g.DrawImage($img, $x, $y, $img.Width, $img.Height)
        $i++
    }
    $sheet.Save((Join-Path $PreviewDir 'hud_icon_preview.png'))
    $g.Dispose()
}
