<#
.SYNOPSIS
    Generates assets/app.ico.

.DESCRIPTION
    A folder outline with a clock face, drawn at 16/24/32/48/64/256 px and packed
    into a single .ico. Small sizes drop the clock hands and keep the dot, because
    hands turn to mud below 24 px.

    Run this only when the icon should change; the result is committed.
#>
[CmdletBinding()]
param(
    [string]$OutFile = (Join-Path (Split-Path -Parent $PSScriptRoot) 'assets\app.ico')
)

$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing

$sizes = @(16, 24, 32, 48, 64, 256)

$folderBack = [System.Drawing.Color]::FromArgb(255, 240, 178, 66)
$folderFront = [System.Drawing.Color]::FromArgb(255, 250, 205, 110)
$clockFace = [System.Drawing.Color]::FromArgb(255, 250, 250, 252)
$clockEdge = [System.Drawing.Color]::FromArgb(255, 54, 66, 88)

function New-IconBitmap([int]$size) {
    $bmp = New-Object System.Drawing.Bitmap $size, $size, ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $g = [System.Drawing.Graphics]::FromImage($bmp)
    $g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $g.Clear([System.Drawing.Color]::Transparent)

    $u = $size / 32.0   # design grid is 32x32

    # Back flap of the folder
    $back = New-Object System.Drawing.Drawing2D.GraphicsPath
    $back.AddPolygon(@(
        (New-Object System.Drawing.PointF (2 * $u), (8 * $u)),
        (New-Object System.Drawing.PointF (12 * $u), (8 * $u)),
        (New-Object System.Drawing.PointF (15 * $u), (11 * $u)),
        (New-Object System.Drawing.PointF (30 * $u), (11 * $u)),
        (New-Object System.Drawing.PointF (30 * $u), (26 * $u)),
        (New-Object System.Drawing.PointF (2 * $u), (26 * $u))
    ))
    $brushBack = New-Object System.Drawing.SolidBrush $folderBack
    $g.FillPath($brushBack, $back)
    $brushBack.Dispose(); $back.Dispose()

    # Front panel, offset so the flap stays visible
    $front = New-Object System.Drawing.Drawing2D.GraphicsPath
    $front.AddPolygon(@(
        (New-Object System.Drawing.PointF (2 * $u), (13 * $u)),
        (New-Object System.Drawing.PointF (30 * $u), (13 * $u)),
        (New-Object System.Drawing.PointF (30 * $u), (27 * $u)),
        (New-Object System.Drawing.PointF (2 * $u), (27 * $u))
    ))
    $brushFront = New-Object System.Drawing.SolidBrush $folderFront
    $g.FillPath($brushFront, $front)
    $brushFront.Dispose(); $front.Dispose()

    # Clock, bottom right, overlapping the folder edge
    $cx = 22 * $u; $cy = 21 * $u; $r = 8.5 * $u
    $g.FillEllipse((New-Object System.Drawing.SolidBrush $clockEdge),
                   ($cx - $r), ($cy - $r), (2 * $r), (2 * $r))
    $inner = $r - [Math]::Max(1.0, 1.4 * $u)
    $g.FillEllipse((New-Object System.Drawing.SolidBrush $clockFace),
                   ($cx - $inner), ($cy - $inner), (2 * $inner), (2 * $inner))

    if ($size -ge 24) {
        # Hands at roughly 10:10, the classic readable position
        $pen = New-Object System.Drawing.Pen $clockEdge, ([Math]::Max(1.0, 1.3 * $u))
        $pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
        $pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
        $g.DrawLine($pen, $cx, $cy, $cx, ($cy - $inner * 0.62))
        $g.DrawLine($pen, $cx, $cy, ($cx + $inner * 0.52), $cy)
        $pen.Dispose()
    } else {
        $dot = [Math]::Max(1.0, 2.0 * $u)
        $g.FillEllipse((New-Object System.Drawing.SolidBrush $clockEdge),
                       ($cx - $dot / 2), ($cy - $dot / 2), $dot, $dot)
    }

    $g.Dispose()
    return $bmp
}

# Classic DIB frames below 256 px, PNG only for 256. GDI+ and some icon readers
# choke on PNG-compressed small frames, and a folder icon nobody can load is
# worse than no icon at all.
function ConvertTo-IconDib([System.Drawing.Bitmap]$bmp) {
    $w = $bmp.Width; $h = $bmp.Height
    $rect = New-Object System.Drawing.Rectangle 0, 0, $w, $h
    $data = $bmp.LockBits($rect, [System.Drawing.Imaging.ImageLockMode]::ReadOnly,
                          [System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $pixels = New-Object byte[] ($data.Stride * $h)
    [System.Runtime.InteropServices.Marshal]::Copy($data.Scan0, $pixels, 0, $pixels.Length)
    $bmp.UnlockBits($data)

    $ms = New-Object System.IO.MemoryStream
    $w2 = New-Object System.IO.BinaryWriter $ms

    $maskStride = [int]([Math]::Floor(($w + 31) / 32)) * 4
    $w2.Write([UInt32]40)            # biSize
    $w2.Write([Int32]$w)
    $w2.Write([Int32](2 * $h))       # colour data + mask
    $w2.Write([UInt16]1)             # biPlanes
    $w2.Write([UInt16]32)            # biBitCount
    $w2.Write([UInt32]0)             # BI_RGB
    $w2.Write([UInt32]($w * $h * 4 + $maskStride * $h))
    $w2.Write([Int32]0); $w2.Write([Int32]0)
    $w2.Write([UInt32]0); $w2.Write([UInt32]0)

    # DIBs are stored bottom-up
    for ($y = $h - 1; $y -ge 0; $y--) {
        $w2.Write($pixels, $y * $data.Stride, $w * 4)
    }
    # AND mask: all zero, the alpha channel does the work
    $zeros = New-Object byte[] ($maskStride * $h)
    $w2.Write($zeros)

    $w2.Flush()
    [byte[]]$bytes = $ms.ToArray()
    $w2.Dispose(); $ms.Dispose()
    # Leading comma: without it PowerShell unrolls the array and the caller ends
    # up with Object[] instead of byte[].
    return , $bytes
}

$frames = @()
foreach ($size in $sizes) {
    $bmp = New-IconBitmap $size
    if ($size -ge 256) {
        $ms = New-Object System.IO.MemoryStream
        $bmp.Save($ms, [System.Drawing.Imaging.ImageFormat]::Png)
        [byte[]]$data = $ms.ToArray()
        $ms.Dispose()
    } else {
        [byte[]]$data = ConvertTo-IconDib $bmp
    }
    $frames += , @{ Size = $size; Bytes = $data }
    $bmp.Dispose()
}
$pngs = $frames

$out = New-Object System.IO.MemoryStream
$w = New-Object System.IO.BinaryWriter $out
$w.Write([UInt16]0)                  # reserved
$w.Write([UInt16]1)                  # type: icon
$w.Write([UInt16]$pngs.Count)

$offset = 6 + 16 * $pngs.Count
foreach ($p in $pngs) {
    $dim = if ($p.Size -ge 256) { 0 } else { $p.Size }
    $w.Write([Byte]$dim)             # width
    $w.Write([Byte]$dim)             # height
    $w.Write([Byte]0)                # palette size
    $w.Write([Byte]0)                # reserved
    $w.Write([UInt16]1)              # colour planes
    $w.Write([UInt16]32)             # bits per pixel
    $w.Write([UInt32]$p.Bytes.Length)
    $w.Write([UInt32]$offset)
    $offset += $p.Bytes.Length
}
foreach ($p in $pngs) { $w.Write($p.Bytes, 0, $p.Bytes.Length) }
$w.Flush()

$dir = Split-Path -Parent $OutFile
if (-not (Test-Path $dir)) { New-Item -ItemType Directory -Path $dir -Force | Out-Null }
[System.IO.File]::WriteAllBytes($OutFile, $out.ToArray())
$w.Dispose(); $out.Dispose()

Write-Host "Wrote $OutFile ($((Get-Item $OutFile).Length) bytes, $($sizes -join '/') px)"
