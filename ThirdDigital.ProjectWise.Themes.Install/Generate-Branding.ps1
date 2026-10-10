#Requires -Version 7.2
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
Add-Type -AssemblyName System.Drawing
$assets = Join-Path $PSScriptRoot 'Branding'
$logo = [Drawing.Image]::FromFile((Join-Path $assets 'third-digital-logo.png'))
try {
    foreach ($kind in @('banner', 'dialog')) {
        $height = if ($kind -eq 'banner') { 58 } else { 312 }
        $bitmap = [Drawing.Bitmap]::new(493, $height, [Drawing.Imaging.PixelFormat]::Format24bppRgb)
        $g = [Drawing.Graphics]::FromImage($bitmap)
        try {
            $g.Clear([Drawing.Color]::White)
            $g.InterpolationMode = [Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
            $g.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
            if ($kind -eq 'banner') {
                # Keep the left-hand area clear for the native page title and description.
                $g.DrawImage($logo, [Drawing.Rectangle]::new(347, 14, 133, 32))
            } else {
                $panel = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(239, 242, 245))
                $ink = [Drawing.SolidBrush]::new([Drawing.Color]::FromArgb(38, 43, 51))
                $font = [Drawing.Font]::new('Segoe UI', 16, [Drawing.FontStyle]::Regular, [Drawing.GraphicsUnit]::Pixel)
                try {
                    $g.FillRectangle($panel, 0, 0, 164, 312)
                    $g.DrawImage($logo, [Drawing.Rectangle]::new(14, 24, 136, 33))
                    $g.DrawString("ProjectWise`nThemes", $font, $ink, [Drawing.PointF]::new(16, 105))
                    $colours = @('#282C34', '#002B36', '#012456', '#FAFAFA', '#FDF6E3')
                    for ($i = 0; $i -lt $colours.Count; $i++) {
                        $swatch = [Drawing.SolidBrush]::new([Drawing.ColorTranslator]::FromHtml($colours[$i]))
                        try { $g.FillRectangle($swatch, 16, (174 + 19 * $i), 132, 12) } finally { $swatch.Dispose() }
                    }
                } finally { $font.Dispose(); $panel.Dispose(); $ink.Dispose() }
            }
            $bitmap.Save((Join-Path $assets "$kind.bmp"), [Drawing.Imaging.ImageFormat]::Bmp)
        } finally { $g.Dispose(); $bitmap.Dispose() }
    }
} finally { $logo.Dispose() }
$licence = Get-Content (Join-Path (Split-Path $PSScriptRoot) 'LICENSE') -Raw
$escaped = $licence.Replace('\', '\\').Replace('{', '\{').Replace('}', '\}').Replace("`r", '').Replace("`n", '\par ')
[IO.File]::WriteAllText((Join-Path $assets 'licence.rtf'), '{\rtf1\ansi\deff0{\fonttbl{\f0 Segoe UI;}}\f0\fs18 ' + $escaped + '}', [Text.Encoding]::ASCII)
