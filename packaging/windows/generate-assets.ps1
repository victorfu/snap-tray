<#
.SYNOPSIS
    Generates MSIX Store assets from snaptray.png source image.

.DESCRIPTION
    Creates all required asset sizes for Microsoft Store submission:
    - Square44x44Logo (44x44, plus scale variants)
    - Square71x71Logo (71x71)
    - Square150x150Logo (150x150)
    - Wide310x150Logo (310x150)
    - SplashScreen (620x300)
    - StoreLogo (50x50)

.PARAMETER SourceImage
    Path to source PNG image (should be at least 620x620).
    Defaults to ../../resources/icons/snaptray.png

.PARAMETER OutputDir
    Output directory for generated assets.
    Defaults to ./assets

.EXAMPLE
    .\generate-assets.ps1
    .\generate-assets.ps1 -SourceImage "custom-icon.png" -OutputDir "build\Assets"
#>

param(
    [string]$SourceImage = "",
    [string]$OutputDir = ""
)

$ErrorActionPreference = "Stop"

# Resolve paths
$ScriptDir = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $SourceImage) {
    $SourceImage = Join-Path $ScriptDir "..\..\resources\icons\snaptray.png"
}
if (-not $OutputDir) {
    $OutputDir = Join-Path $ScriptDir "assets"
}

$SourceImage = [System.IO.Path]::GetFullPath($SourceImage)
$OutputDir = [System.IO.Path]::GetFullPath($OutputDir)

Write-Host "MSIX Asset Generator" -ForegroundColor Cyan
Write-Host "====================" -ForegroundColor Cyan
Write-Host "Source: $SourceImage"
Write-Host "Output: $OutputDir"
Write-Host ""

# Validate source exists
if (-not (Test-Path $SourceImage)) {
    Write-Error "Source image not found: $SourceImage"
    exit 1
}

# Create output directory
if (-not (Test-Path $OutputDir)) {
    New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
}

# Asset specifications: [filename, width, height]
$Assets = @(
    @("Square44x44Logo.png", 44, 44),
    @("Square44x44Logo.scale-100.png", 44, 44),
    @("Square44x44Logo.scale-125.png", 55, 55),
    @("Square44x44Logo.scale-150.png", 66, 66),
    @("Square44x44Logo.scale-200.png", 88, 88),
    @("Square44x44Logo.scale-400.png", 176, 176),
    @("Square44x44Logo.targetsize-16.png", 16, 16),
    @("Square44x44Logo.targetsize-24.png", 24, 24),
    @("Square44x44Logo.targetsize-32.png", 32, 32),
    @("Square44x44Logo.targetsize-48.png", 48, 48),
    @("Square44x44Logo.targetsize-256.png", 256, 256),
    @("Square71x71Logo.png", 71, 71),
    @("Square71x71Logo.scale-100.png", 71, 71),
    @("Square71x71Logo.scale-125.png", 89, 89),
    @("Square71x71Logo.scale-150.png", 107, 107),
    @("Square71x71Logo.scale-200.png", 142, 142),
    @("Square71x71Logo.scale-400.png", 284, 284),
    @("Square150x150Logo.png", 150, 150),
    @("Square150x150Logo.scale-100.png", 150, 150),
    @("Square150x150Logo.scale-125.png", 188, 188),
    @("Square150x150Logo.scale-150.png", 225, 225),
    @("Square150x150Logo.scale-200.png", 300, 300),
    @("Square150x150Logo.scale-400.png", 600, 600),
    @("Wide310x150Logo.png", 310, 150),
    @("Wide310x150Logo.scale-100.png", 310, 150),
    @("Wide310x150Logo.scale-125.png", 388, 188),
    @("Wide310x150Logo.scale-150.png", 465, 225),
    @("Wide310x150Logo.scale-200.png", 620, 300),
    @("Wide310x150Logo.scale-400.png", 1240, 600),
    @("SplashScreen.png", 620, 300),
    @("SplashScreen.scale-100.png", 620, 300),
    @("SplashScreen.scale-125.png", 775, 375),
    @("SplashScreen.scale-150.png", 930, 450),
    @("SplashScreen.scale-200.png", 1240, 600),
    @("StoreLogo.png", 50, 50),
    @("StoreLogo.scale-100.png", 50, 50),
    @("StoreLogo.scale-125.png", 63, 63),
    @("StoreLogo.scale-150.png", 75, 75),
    @("StoreLogo.scale-200.png", 100, 100),
    @("StoreLogo.scale-400.png", 200, 200),
    @("LargeTile.scale-100.png", 310, 310),
    @("LargeTile.scale-125.png", 388, 388),
    @("LargeTile.scale-150.png", 465, 465),
    @("LargeTile.scale-200.png", 620, 620),
    @("LargeTile.scale-400.png", 1240, 1240),
    @("SmallTile.scale-100.png", 71, 71),
    @("SmallTile.scale-125.png", 89, 89),
    @("SmallTile.scale-150.png", 107, 107),
    @("SmallTile.scale-200.png", 142, 142),
    @("SmallTile.scale-400.png", 284, 284)
)

# Check for ImageMagick
$magick = Get-Command magick -ErrorAction SilentlyContinue
if (-not $magick) {
    Write-Host "ImageMagick not found. Using System.Drawing..." -ForegroundColor Yellow
    Write-Host ""

    # Use .NET System.Drawing as fallback
    Add-Type -AssemblyName System.Drawing

    function Resize-Image {
        param([string]$Source, [string]$Dest, [int]$Width, [int]$Height)

        $srcImage = [System.Drawing.Image]::FromFile($Source)
        $destImage = New-Object System.Drawing.Bitmap($Width, $Height)
        $graphics = [System.Drawing.Graphics]::FromImage($destImage)
        $graphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
        $graphics.SmoothingMode = [System.Drawing.Drawing2D.SmoothingMode]::HighQuality
        $graphics.PixelOffsetMode = [System.Drawing.Drawing2D.PixelOffsetMode]::HighQuality

        # For wide/splash images, center the square logo
        if ($Width -gt $Height) {
            $srcSize = [Math]::Min($srcImage.Width, $srcImage.Height)
            $scaleFactor = $Height / $srcSize
            $scaledSize = [int]($srcSize * $scaleFactor)
            $x = [int](($Width - $scaledSize) / 2)
            $graphics.Clear([System.Drawing.Color]::Transparent)
            $graphics.DrawImage($srcImage, $x, 0, $scaledSize, $Height)
        } else {
            $graphics.DrawImage($srcImage, 0, 0, $Width, $Height)
        }

        $destImage.Save($Dest, [System.Drawing.Imaging.ImageFormat]::Png)
        $graphics.Dispose()
        $destImage.Dispose()
        $srcImage.Dispose()
    }

    $successCount = 0
    $failCount = 0

    foreach ($asset in $Assets) {
        $filename = $asset[0]
        $width = $asset[1]
        $height = $asset[2]
        $outputPath = Join-Path $OutputDir $filename

        Write-Host "  Generating $filename ($width x $height)..." -NoNewline
        try {
            Resize-Image -Source $SourceImage -Dest $outputPath -Width $width -Height $height
            Write-Host " OK" -ForegroundColor Green
            $successCount++
        } catch {
            Write-Host " FAILED: $_" -ForegroundColor Red
            $failCount++
        }
    }
} else {
    Write-Host "Using ImageMagick for asset generation" -ForegroundColor Green
    Write-Host ""

    $successCount = 0
    $failCount = 0

    foreach ($asset in $Assets) {
        $filename = $asset[0]
        $width = $asset[1]
        $height = $asset[2]
        $outputPath = Join-Path $OutputDir $filename

        Write-Host "  Generating $filename ($width x $height)..." -NoNewline
        try {
            if ($width -gt $height) {
                # Wide image: resize to fit height, center horizontally
                & magick $SourceImage -resize "x$height" -gravity center -background transparent -extent "${width}x${height}" $outputPath 2>$null
            } else {
                # Square image: simple resize
                & magick $SourceImage -resize "${width}x${height}!" $outputPath 2>$null
            }
            if ($LASTEXITCODE -eq 0) {
                Write-Host " OK" -ForegroundColor Green
                $successCount++
            } else {
                Write-Host " FAILED" -ForegroundColor Red
                $failCount++
            }
        } catch {
            Write-Host " FAILED: $_" -ForegroundColor Red
            $failCount++
        }
    }
}

Write-Host ""
Write-Host "Asset generation complete!" -ForegroundColor Green
Write-Host "  Success: $successCount"
if ($failCount -gt 0) {
    Write-Host "  Failed:  $failCount" -ForegroundColor Red
}
Write-Host "  Output:  $OutputDir"
