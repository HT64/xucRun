# Purpose: Creates an xucRun icon variant as a multi-resolution ICO and 1024-pixel JPG file.
param(
    [string]$OutputPath = (Join-Path $PSScriptRoot "xucRun.ico"),
    [string]$JpegOutputPath = (Join-Path $PSScriptRoot "xucRun-1024.jpg"),
    [ValidateSet('Client', 'Broker', 'Approval')]
    [string]$Variant = 'Client'
)

Add-Type -AssemblyName System.Drawing

function New-AppIconBitmap {
    param(
        [int]$Size,
        [System.Drawing.Color]$CanvasColor,
        [double]$ContentScale = 1.0
    )

    $bitmap = New-Object System.Drawing.Bitmap $size, $size,
        ([System.Drawing.Imaging.PixelFormat]::Format32bppArgb)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
    $graphics.Clear($CanvasColor)

    $scale = $size / 32.0
    if ($ContentScale -ne 1.0) {
        $offset = ($size / 2.0) * (1.0 - $ContentScale)
        $graphics.Transform = New-Object System.Drawing.Drawing2D.Matrix(
            $ContentScale, 0, 0, $ContentScale, $offset, $offset)
    }
    $backgroundColor = switch ($Variant) {
        'Broker' { [System.Drawing.Color]::FromArgb(255, 13, 48, 39) }
        'Approval' { [System.Drawing.Color]::FromArgb(255, 112, 52, 61) }
        default { [System.Drawing.Color]::FromArgb(255, 43, 105, 121) }
    }
    $borderColor = switch ($Variant) {
        'Broker' { [System.Drawing.Color]::FromArgb(255, 233, 80, 45) }
        'Approval' { [System.Drawing.Color]::FromArgb(255, 232, 123, 105) }
        default { [System.Drawing.Color]::FromArgb(255, 58, 190, 181) }
    }
    $background = New-Object System.Drawing.SolidBrush $backgroundColor
    $border = New-Object System.Drawing.Pen $borderColor, (1.5 * $scale)
    $mark = New-Object System.Drawing.Pen ([System.Drawing.Color]::White), (3.2 * $scale)
    $accent = New-Object System.Drawing.SolidBrush ([System.Drawing.Color]::FromArgb(255, 244, 180, 63))
    $detail = New-Object System.Drawing.SolidBrush $backgroundColor

    $path = New-Object System.Drawing.Drawing2D.GraphicsPath
    $radius = 6 * $scale
    $diameter = $radius * 2
    $bounds = New-Object System.Drawing.RectangleF (1 * $scale), (1 * $scale), (30 * $scale), (30 * $scale)
    $path.AddArc($bounds.Left, $bounds.Top, $diameter, $diameter, 180, 90)
    $path.AddArc($bounds.Right - $diameter, $bounds.Top, $diameter, $diameter, 270, 90)
    $path.AddArc($bounds.Right - $diameter, $bounds.Bottom - $diameter, $diameter, $diameter, 0, 90)
    $path.AddArc($bounds.Left, $bounds.Bottom - $diameter, $diameter, $diameter, 90, 90)
    $path.CloseFigure()
    $graphics.FillPath($background, $path)
    $graphics.DrawPath($border, $path)

    if ($Variant -eq 'Broker') {
        $graphics.DrawLine($mark, 7 * $scale, 8 * $scale, 17 * $scale, 24 * $scale)
        $graphics.DrawLine($mark, 17 * $scale, 8 * $scale, 7 * $scale, 24 * $scale)
        $graphics.FillPolygon($accent, [System.Drawing.PointF[]]@(
            (New-Object System.Drawing.PointF (22 * $scale), (9 * $scale)),
            (New-Object System.Drawing.PointF (28 * $scale), (12 * $scale)),
            (New-Object System.Drawing.PointF (27 * $scale), (20 * $scale)),
            (New-Object System.Drawing.PointF (22 * $scale), (25 * $scale)),
            (New-Object System.Drawing.PointF (17 * $scale), (20 * $scale)),
            (New-Object System.Drawing.PointF (16 * $scale), (12 * $scale))
        ))
        $graphics.FillEllipse($detail,
            20.2 * $scale, 14.1 * $scale, 3.6 * $scale, 3.6 * $scale)
        $graphics.FillRectangle($detail,
            21.2 * $scale, 17 * $scale, 1.6 * $scale, 4 * $scale)
    }
    else {
        $graphics.DrawLine($mark, 8 * $scale, 8 * $scale, 20 * $scale, 24 * $scale)
        $graphics.DrawLine($mark, 20 * $scale, 8 * $scale, 8 * $scale, 24 * $scale)
        $graphics.FillPolygon($accent, [System.Drawing.PointF[]]@(
            (New-Object System.Drawing.PointF (18 * $scale), (11 * $scale)),
            (New-Object System.Drawing.PointF (27 * $scale), (16 * $scale)),
            (New-Object System.Drawing.PointF (18 * $scale), (21 * $scale))
        ))
    }

    $path.Dispose()
    $detail.Dispose()
    $accent.Dispose()
    $mark.Dispose()
    $border.Dispose()
    $background.Dispose()
    $graphics.Dispose()

    return $bitmap
}

$images = foreach ($size in 16, 32, 48, 256) {
    $bitmap = New-AppIconBitmap -Size $size -CanvasColor ([System.Drawing.Color]::Transparent) `
        -ContentScale 1.06
    $stream = New-Object System.IO.MemoryStream
    $bitmap.Save($stream, [System.Drawing.Imaging.ImageFormat]::Png)
    $bytes = $stream.ToArray()

    $stream.Dispose()
    $bitmap.Dispose()

    [pscustomobject]@{ Size = $size; Bytes = $bytes }
}

$output = New-Object System.IO.MemoryStream
$writer = New-Object System.IO.BinaryWriter $output
$writer.Write([uint16]0)
$writer.Write([uint16]1)
$writer.Write([uint16]$images.Count)

$offset = 6 + (16 * $images.Count)
foreach ($image in $images) {
    $writer.Write([byte]$(if ($image.Size -eq 256) { 0 } else { $image.Size }))
    $writer.Write([byte]$(if ($image.Size -eq 256) { 0 } else { $image.Size }))
    $writer.Write([byte]0)
    $writer.Write([byte]0)
    $writer.Write([uint16]1)
    $writer.Write([uint16]32)
    $writer.Write([uint32]$image.Bytes.Length)
    $writer.Write([uint32]$offset)
    $offset += $image.Bytes.Length
}

foreach ($image in $images) {
    $writer.Write($image.Bytes)
}

$writer.Flush()
[System.IO.File]::WriteAllBytes($OutputPath, $output.ToArray())
$writer.Dispose()
$output.Dispose()

$jpegBitmap = New-AppIconBitmap -Size 1024 -CanvasColor ([System.Drawing.Color]::White)
$jpegBitmap.Save($JpegOutputPath, [System.Drawing.Imaging.ImageFormat]::Jpeg)
$jpegBitmap.Dispose()