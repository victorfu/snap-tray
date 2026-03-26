---
last_modified_at: 2026-03-24
layout: docs
title: 發佈與打包
description: 產生 macOS 與 Windows 可發佈安裝檔，並整理簽章、公證與商店提交流程。
permalink: /zh-tw/developer/release-packaging/
lang: zh-tw
route_key: developer_release_packaging
nav_data: developer_nav
docs_copy_key: developer
---

## 打包腳本

打包腳本會自動執行 Release 建置、部署 Qt 相依套件，並產出可安裝的發佈物。

### macOS

```bash
./packaging/macos/package.sh
# 輸出：dist/SnapTray-<version>-macOS.dmg
```

可選工具：

```bash
brew install create-dmg
```

### Windows

```batch
packaging\windows\package.bat           REM 同時建置 NSIS 與 MSIX
packaging\windows\package.bat nsis      REM 只建置 NSIS
packaging\windows\package.bat msix      REM 只建置 MSIX
```

典型輸出：

- `dist\SnapTray-<version>-Setup.exe`
- `dist\SnapTray-<version>.msix`
- `dist\SnapTray-<version>.msixupload`

## 打包前置需求

### macOS

- 已安裝 Qt 6
- Xcode Command Line Tools
- 可選 `create-dmg` 以美化 DMG 視覺

### Windows

- 已安裝 Qt 6
- Visual Studio Build Tools 或 Visual Studio
- NSIS（產出 NSIS 安裝程式）
- Windows 10 SDK（產出 MSIX）

若 Qt 不是安裝在預設路徑，可設定：

```batch
set QT_PATH=C:\Qt\6.10.1\msvc2022_64
```

## 簽章與公證

未簽章安裝檔在作業系統上會產生信任警告。只要是公開發佈，強烈建議完成簽章流程。

### macOS 簽章與公證

一次性儲存 notarytool 憑證：

```bash
xcrun notarytool store-credentials "snaptray-notary" \
  --apple-id "your@email.com" \
  --team-id "YOURTEAMID" \
  --password "xxxx-xxxx-xxxx-xxxx"
```

建置、簽章、公證與 staple：

```bash
export CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
export NOTARIZE_KEYCHAIN_PROFILE="snaptray-notary"
./packaging/macos/package.sh
```

驗證 Gatekeeper：

```bash
spctl -a -vv -t open "dist/SnapTray-<version>-macOS.dmg"
```

舊式 CI 環境變數仍可使用：

```bash
export NOTARIZE_APPLE_ID="your@email.com"
export NOTARIZE_TEAM_ID="YOURTEAMID"
export NOTARIZE_PASSWORD="xxxx-xxxx-xxxx-xxxx"
```

### Windows 簽章

NSIS 安裝程式簽章：

```batch
set CODESIGN_CERT=path\to\certificate.pfx
set CODESIGN_PASSWORD=your-password
packaging\windows\package.bat nsis
```

MSIX 側載 / 企業部署簽章：

```batch
set MSIX_SIGN_CERT=path\to\certificate.pfx
set MSIX_SIGN_PASSWORD=your-password
set PUBLISHER_ID=CN=SnapTray Dev
packaging\windows\package.bat msix
```

若是上架 Microsoft Store，Microsoft 會在認證階段替你簽章，因此不需要額外為 Store 流程做 MSIX 簽章。

## MSIX 本地測試

安裝未簽章 MSIX：

```powershell
Add-AppPackage -Path "dist\SnapTray-<version>.msix" -AllowUnsigned
```

解除安裝：

```powershell
Get-AppPackage SnapTray* | Remove-AppPackage
```

## Microsoft Store 提交

1. 登入 Partner Center
2. 建立或選擇 app reservation
3. 建立新的 submission
4. 上傳 `.msixupload`
5. 補齊 listing、pricing、age rating
6. 送出認證

## App 圖示流程

請先準備 1024x1024 的來源 PNG，再生成平台所需資產。

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

## 與發佈相關的網站內容

- `docs/updates/*.xml` 是 Sparkle / WinSparkle 使用的 appcast feeds
- GitHub Actions release workflow 會更新網站上的 release pages 與 appcast 檔案

## 下一步

- 回到[從原始碼建置](build-from-source.md)處理本地開發
- 前往[架構總覽](architecture.md)掌握 repo 與 subsystem 結構
