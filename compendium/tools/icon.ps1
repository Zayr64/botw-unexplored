# Draws compendium/icon.jpg (256x256): a golden cooking pot with steam on the dark slate of BotW Unexplored's icon.
# Usage: powershell -File compendium/tools/icon.ps1 <output.jpg>
param([string]$Out = "icon.jpg")
Add-Type -AssemblyName System.Drawing
$size = 256
$bmp = New-Object System.Drawing.Bitmap $size, $size
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$g.Clear([System.Drawing.Color]::FromArgb(20, 28, 33))
$gold = [System.Drawing.Color]::FromArgb(214, 180, 96)
$brush = New-Object System.Drawing.SolidBrush $gold
$pen = New-Object System.Drawing.Pen $gold, 9
$pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round

# Body: the lower half of an ellipse under a flat rim.
$body = New-Object System.Drawing.Drawing2D.GraphicsPath
$body.AddArc(48, 70, 160, 150, 0, 180)
$body.CloseFigure()
$g.FillPath($brush, $body)
# Rim and handles.
$g.FillRectangle($brush, 40, 136, 176, 14)
$g.DrawArc($pen, 18, 140, 34, 30, 90, 180)
$g.DrawArc($pen, 204, 140, 34, 30, 270, 180)
# Steam.
foreach ($x in 92, 128, 164) {
    $steam = New-Object System.Drawing.Drawing2D.GraphicsPath
    $steam.AddBezier($x, 118, $x - 14, 98, $x + 14, 82, $x, 60)
    $g.DrawPath($pen, $steam)
}

$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq "image/jpeg" }
$params = New-Object System.Drawing.Imaging.EncoderParameters 1
$params.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), 92L
$bmp.Save($Out, $codec, $params)
$g.Dispose(); $bmp.Dispose()
Write-Output "wrote $Out"
