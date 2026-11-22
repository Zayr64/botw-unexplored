# Draws compendium/icon.jpg (256x256): an open golden book, with lines of text on its pages, on the dark slate and in
# the gold of BotW Unexplored's icon. Chosen by the user on 22/11/2026 among a pot, a book and a book with a pot.
# Usage: powershell -File compendium/tools/icon.ps1 <output.jpg>
param([string]$Out = "icon.jpg")
Add-Type -AssemblyName System.Drawing
$bmp = New-Object System.Drawing.Bitmap 256, 256
$g = [System.Drawing.Graphics]::FromImage($bmp)
$g.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::AntiAlias
$dark = [System.Drawing.Color]::FromArgb(20, 28, 33)
$gold = [System.Drawing.Color]::FromArgb(214, 180, 96)
$g.Clear($dark)
$brush = New-Object System.Drawing.SolidBrush $gold

# Two pages meeting at the spine, their top and bottom edges slightly curved.
$left = New-Object System.Drawing.Drawing2D.GraphicsPath
$left.AddBezier(128, 76, 104, 62, 62, 58, 30, 66)
$left.AddLine(30, 66, 30, 190)
$left.AddBezier(30, 190, 62, 182, 104, 186, 128, 200)
$left.CloseFigure()
$right = New-Object System.Drawing.Drawing2D.GraphicsPath
$right.AddBezier(128, 76, 152, 62, 194, 58, 226, 66)
$right.AddLine(226, 66, 226, 190)
$right.AddBezier(226, 190, 194, 182, 152, 186, 128, 200)
$right.CloseFigure()
$g.FillPath($brush, $left)
$g.FillPath($brush, $right)

$pen = New-Object System.Drawing.Pen $dark, 6
$pen.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$pen.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$spine = New-Object System.Drawing.Pen $dark, 5
$spine.StartCap = [System.Drawing.Drawing2D.LineCap]::Round
$spine.EndCap = [System.Drawing.Drawing2D.LineCap]::Round
$g.DrawLine($spine, 128, 80, 128, 196)
foreach ($y in 100, 122, 144, 166) {
    $g.DrawLine($pen, 48, $y, 110, $y + 2)
    $g.DrawLine($pen, 146, $y + 2, 208, $y)
}

$codec = [System.Drawing.Imaging.ImageCodecInfo]::GetImageEncoders() | Where-Object { $_.MimeType -eq "image/jpeg" }
$params = New-Object System.Drawing.Imaging.EncoderParameters 1
$params.Param[0] = New-Object System.Drawing.Imaging.EncoderParameter ([System.Drawing.Imaging.Encoder]::Quality), 92L
$bmp.Save($Out, $codec, $params)
$g.Dispose(); $bmp.Dispose()
Write-Output "wrote $Out"
