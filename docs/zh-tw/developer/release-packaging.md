---
last_modified_at: 2026-03-27
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

## Release Notes 與版本規則

SnapTray 的發佈版本由 CMake 管理，版本真源是 `CMakeLists.txt` 裡的 `project(SnapTray VERSION ...)`：

```cmake
project(SnapTray VERSION <version> LANGUAGES CXX)
```

人工整理過的 release notes 放在 repo 根目錄的 `CHANGELOG.md`。

- 正式版本段落請使用 `## [1.0.42] - 2026-03-27` 這種格式
- 可以保留一個 `## [Unreleased]` 區段放在最上方
- GitHub Releases 與網站 release page 都會使用 `CHANGELOG.md` 裡對應版本的段落
- changelog 請維持在標題、段落、單層清單、粗斜體與連結這類簡單 markdown，避免網站 release renderer 呈現失真

一般 tag 發版流程，例如 `v1.0.42`：

1. 先把 `CMakeLists.txt` 更新到新版本
2. 新增或更新 `CHANGELOG.md` 的對應版本段落
3. 建立本地 tag：

```bash
git tag v1.0.42
```

4. 準備好要觸發 CI 時，再手動 push release tag。
   例如：`git push origin v1.0.42`

之後 release workflow 會：

- 驗證 tag 版本與 `CMakeLists.txt` 是否一致
- 從 `CHANGELOG.md` 抽出對應版本段落作為 GitHub Release notes
- 建置 macOS 與 Windows 發佈物
- 對發佈資產進行簽章 / 公證
- 發布 GitHub Release
- 更新 appcasts 與網站 release page

不要為 SnapTray release 文件加入 Xcode 或 `MARKETING_VERSION` 的說明。這個 repo 的發版版本不是由 Xcode project 管理。

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
- `scripts/extract_changelog_entry.py` 會從 `CHANGELOG.md` 抽出特定版本的 release notes
- GitHub Actions release workflow 會更新 GitHub Releases、網站 release pages 與 appcast 檔案

## 下一步

- 回到[從原始碼建置](build-from-source.md)處理本地開發
- 前往[架構總覽](architecture.md)掌握 repo 與 subsystem 結構
