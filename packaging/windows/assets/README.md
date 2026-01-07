# MSIX Store Assets

This directory contains Microsoft Store assets generated from `snaptray.png`.

## Regenerating Assets

To regenerate assets after updating the source icon:

```powershell
cd packaging\windows
.\generate-assets.ps1
```

### Prerequisites

One of the following:
- **ImageMagick** (recommended): `winget install ImageMagick.ImageMagick`
- **.NET Framework** (built-in fallback): No installation needed

### Custom Source Image

To use a different source image:

```powershell
.\generate-assets.ps1 -SourceImage "path\to\custom-icon.png"
```

## Asset Sizes

| Asset | Size(s) | Purpose |
|-------|---------|---------|
| Square44x44Logo | 44x44 + scales | App list icon |
| Square71x71Logo | 71x71 + scales | Small tile |
| Square150x150Logo | 150x150 + scales | Medium tile |
| Wide310x150Logo | 310x150 + scales | Wide tile |
| SplashScreen | 620x300 + scales | Splash screen |
| StoreLogo | 50x50 + scales | Store listing |

## Manual Editing

You can manually replace any generated asset with a custom design.
Ensure the dimensions match exactly.
