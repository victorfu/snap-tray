---
layout: docs
title: Release & Packaging
description: Create distributable macOS and Windows packages, sign them, and prepare app-store or direct-download releases.
permalink: /developer/release-packaging/
lang: en
route_key: developer_release_packaging
nav_data: developer_nav
docs_copy_key: developer
---

## Packaging scripts

Packaging scripts run Release builds, deploy Qt dependencies, and produce installable artifacts.

### macOS

```bash
./packaging/macos/package.sh
# Output: dist/SnapTray-<version>-macOS.dmg
```

Optional helper:

```bash
brew install create-dmg
```

### Windows

```batch
packaging\windows\package.bat           REM Build both NSIS and MSIX
packaging\windows\package.bat nsis      REM NSIS installer only
packaging\windows\package.bat msix      REM MSIX package only
```

Typical outputs:

- `dist\SnapTray-<version>-Setup.exe`
- `dist\SnapTray-<version>.msix`
- `dist\SnapTray-<version>.msixupload`

## Packaging prerequisites

### macOS

- Qt 6 installed
- Xcode Command Line Tools
- Optional `create-dmg` for nicer DMG presentation

### Windows

- Qt 6 installed
- Visual Studio Build Tools or Visual Studio
- NSIS for NSIS installers
- Windows 10 SDK for MSIX packaging

If Qt is not installed in the default location, set:

```batch
set QT_PATH=C:\Qt\6.10.1\msvc2022_64
```

## Code signing and notarization

Unsigned installers will generate OS trust warnings. Signed artifacts are strongly recommended for any public distribution.

### macOS signing and notarization

One-time credential setup:

```bash
xcrun notarytool store-credentials "snaptray-notary" \
  --apple-id "your@email.com" \
  --team-id "YOURTEAMID" \
  --password "xxxx-xxxx-xxxx-xxxx"
```

Build, sign, notarize, and staple:

```bash
export CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
export NOTARIZE_KEYCHAIN_PROFILE="snaptray-notary"
./packaging/macos/package.sh
```

Verify Gatekeeper acceptance:

```bash
spctl -a -vv -t open "dist/SnapTray-<version>-macOS.dmg"
```

Legacy CI-style environment variables remain supported:

```bash
export NOTARIZE_APPLE_ID="your@email.com"
export NOTARIZE_TEAM_ID="YOURTEAMID"
export NOTARIZE_PASSWORD="xxxx-xxxx-xxxx-xxxx"
```

### Windows signing

NSIS installer signing:

```batch
set CODESIGN_CERT=path\to\certificate.pfx
set CODESIGN_PASSWORD=your-password
packaging\windows\package.bat nsis
```

MSIX sideload / enterprise signing:

```batch
set MSIX_SIGN_CERT=path\to\certificate.pfx
set MSIX_SIGN_PASSWORD=your-password
set PUBLISHER_ID=CN=SnapTray Dev
packaging\windows\package.bat msix
```

For Microsoft Store submission, Microsoft signs the package during certification, so separate MSIX signing is not required for Store delivery.

## Local MSIX testing

Install an unsigned MSIX locally:

```powershell
Add-AppPackage -Path "dist\SnapTray-<version>.msix" -AllowUnsigned
```

Uninstall:

```powershell
Get-AppPackage SnapTray* | Remove-AppPackage
```

## Microsoft Store submission

1. Sign in to Partner Center
2. Create or select the app reservation
3. Create a new submission
4. Upload the `.msixupload`
5. Complete listing, pricing, and age rating details
6. Submit for certification

## App icon workflow

Prepare a 1024x1024 source PNG and regenerate the platform packaging assets from it.

### macOS `.icns`

```bash
cd resources/icons
mkdir snaptray.iconset
sips -z 16 16     snaptray.png --out snaptray.iconset/icon_16x16.png
sips -z 32 32     snaptray.png --out snaptray.iconset/icon_16x16@2x.png
sips -z 32 32     snaptray.png --out snaptray.iconset/icon_32x32.png
sips -z 64 64     snaptray.png --out snaptray.iconset/icon_32x32@2x.png
sips -z 128 128   snaptray.png --out snaptray.iconset/icon_128x128.png
sips -z 256 256   snaptray.png --out snaptray.iconset/icon_128x128@2x.png
sips -z 256 256   snaptray.png --out snaptray.iconset/icon_256x256.png
sips -z 512 512   snaptray.png --out snaptray.iconset/icon_256x256@2x.png
sips -z 512 512   snaptray.png --out snaptray.iconset/icon_512x512.png
sips -z 1024 1024 snaptray.png --out snaptray.iconset/icon_512x512@2x.png
iconutil -c icns snaptray.iconset
rm -rf snaptray.iconset
```

### Windows `.ico`

```bash
magick snaptray.png -define icon:auto-resize=256,128,64,48,32,16 snaptray.ico
```

## Related release infrastructure

- `docs/updates/*.xml` holds appcast feeds used by Sparkle / WinSparkle release flows
- GitHub Actions release automation populates release pages and appcast files for the website

## Next step

- Go to [Build from Source](build-from-source.md) for local dev setup
- Go to [Architecture Overview](architecture.md) for repository and subsystem structure
