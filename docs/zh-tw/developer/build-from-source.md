---
last_modified_at: 2026-03-26
layout: docs
title: 從原始碼建置
description: 在 macOS 或 Windows 依官方支援方式建置 SnapTray，包含腳本流程、手動 CMake 與編譯快取設定。
permalink: /zh-tw/developer/build-from-source/
lang: zh-tw
route_key: developer_build_from_source
nav_data: developer_nav
docs_copy_key: developer
---

## 支援平台

SnapTray 目前僅支援 macOS 與 Windows。

### 執行目標

- macOS 14.0+
- Windows 10+

### 開發前置需求

- Qt 6.10.1，需包含 Widgets、Gui、Svg、Concurrent、Network、Quick、QuickControls2、QuickWidgets
- CMake 3.16+
- Ninja
- Git（FetchContent 相依套件）
- macOS：Xcode Command Line Tools
- Windows：Visual Studio 2022 Build Tools 與 Windows SDK

### 自動抓取的相依套件

- [QHotkey](https://github.com/Skycoder42/QHotkey)：全域熱鍵
- [OpenCV 4.10.0](https://github.com/opencv/opencv)：臉部與結構偵測
- [libwebp 1.3.2](https://github.com/webmproject/libwebp)：WebP 動畫編碼
- [ZXing-CPP v2.2.1](https://github.com/zxing-cpp/zxing-cpp)：QR / 條碼處理

## 建議使用的建置腳本

日常開發請優先使用 repo 內建腳本，這是目前最受支援的建置與驗證流程。

### macOS

```bash
./scripts/build.sh                  # Debug 建置
./scripts/build-release.sh          # Release 建置
./scripts/run-tests.sh              # 建置並執行測試
./scripts/build-and-run.sh          # Debug 建置 + 執行 app
./scripts/build-and-run-release.sh  # Release 建置 + 執行 app
```

### Windows

```batch
scripts\build.bat                   REM Debug 建置
scripts\build-release.bat           REM Release 建置
scripts\run-tests.bat               REM 建置並執行測試
scripts\build-and-run.bat           REM Debug 建置 + 執行 app
scripts\build-and-run-release.bat   REM Release 建置 + 執行 app
```

`scripts\run-tests.bat` 會在執行 `ctest` 前先把 `%QT_PATH%\bin` 加到 `PATH` 前面。若你要在 Windows 的 debug build 手動跑測試，也要先做同樣設定，否則 `Qt6Testd.dll` 與其他 Qt debug DLL 可能找不到。

日常驗證以 `build.sh` / `build.bat` 與測試腳本為主；只有在需要手動 UI 驗證時才一定要執行 app。

## PowerShell 與 MSVC

如果你在 Windows 用 PowerShell 建置，先載入 Visual Studio developer shell：

```powershell
Import-Module "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" -DevCmdArguments '-arch=x64' -SkipAutomaticLocation
```

## 手動 CMake 流程

只有在需要自訂旗標或排查建置問題時，才建議改用手動 configure/build。

### macOS（Debug）

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
# 產物：build/bin/SnapTray-Debug.app
```

### macOS（Release）

```bash
cmake -S . -B release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build release --parallel
# 產物：release/bin/SnapTray.app
```

### Windows（Debug）

```batch
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/msvc2022_64
cmake --build build --parallel
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

### Windows（Release）

```batch
cmake -S . -B release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/msvc2022_64
cmake --build release --parallel
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe --release release\bin\SnapTray.exe
```

## 建置模式與輸出

- `Debug` build 在 macOS 會使用 `SnapTray-Debug` 顯示名稱與 debug bundle identifier
- `Release` build 使用正式名稱 `SnapTray`
- MCP 僅在 `Debug` build 透過 `SNAPTRAY_ENABLE_MCP` 編譯進去

## 建置優化

建置系統會在工具存在時自動啟用編譯器快取。

### 本地快取工具

#### macOS

```bash
brew install ccache
```

#### Windows

```bash
scoop install sccache
```

### 目前的優化策略

| 優化 | 位置 | 效果 |
|---|---|---|
| `ccache` / `sccache` 自動偵測 | `CMakeLists.txt` | 加快增量建置 |
| MSVC 使用 `/Z7` | `CMakeLists.txt` | 提升 Windows debug build 的快取命中 |
| FetchContent 快取 | CI | 降低第三方依賴重建成本 |

由於 Objective-C++ 與整體相容性考量，專案刻意不採用 PCH 與 Unity Build。

## 本地驗證建議

推薦流程：

1. 跑平台對應 build script
2. 跑平台對應 test script
3. 只有在 UI 或 runtime 需要人工確認時才開 app

在 Windows debug build 上若手動執行 `ctest`，請讓 `PATH` 以 `%QT_PATH%\bin` 開頭，做法與 `scripts\run-tests.bat` 一致。

## 建置排錯

### macOS：Gatekeeper 擋住 app

正式簽章與公證完成的版本理論上不會有 Gatekeeper 警告。若是本地 ad-hoc build，可在必要時清除 quarantine：

```bash
xattr -cr /Applications/SnapTray.app
```

### Windows：缺少 DLL 或 Qt platform plugin

若出現 `Qt6Core.dll was not found` 或 `no Qt platform plugin could be initialized`，請執行 `windeployqt`：

```batch
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

若 Qt 安裝在其他位置，請改成你的實際路徑。

## 下一步

- 需要產出安裝檔時，前往[發佈與打包](release-packaging.md)
- 需要了解專案結構與 subsystem 時，前往[架構總覽](architecture.md)
