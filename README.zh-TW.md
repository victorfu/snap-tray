# SnapTray - 系統托盤截圖與錄影工具

[English](README.md) | **繁體中文**

SnapTray 是一個在系統托盤常駐的截圖與錄影小工具，提供區域截圖、螢幕標註與快速錄影。預設以 F2 進入區域截圖，Ctrl+F2 開啟螢幕畫布。

## 功能特色

- **系統托盤選單**：`Region Capture` (顯示當前熱鍵)、`Screen Canvas` (顯示當前熱鍵)、`Close All Pins`、`Exit Click-through`、`Settings`、`Exit`
- **全域快捷鍵**：可於設定中自定義，支援即時更新熱鍵註冊。
  - 區域截圖：預設 `F2`
  - 螢幕畫布：預設 `Ctrl+F2`
  - 切換點擊穿透（釘選視窗）：`Shift+T` (切換游標下方釘選視窗的點擊穿透模式)
- **區域截圖覆蓋層**：
  - 十字線＋放大鏡（支援像素級檢視）
  - RGB/HEX 顏色預覽（按 Shift 切換，按 C 複製顏色代碼）
  - 尺寸標示
  - 選取框控制點（類 Snipaste 風格）
  - 視窗偵測（macOS/Windows）：自動偵測游標下的視窗，單擊快速選取
- **截圖工具列**：
  - `Selection` 選取工具（調整選取區域）
  - 標註工具：`Arrow` / `Pencil` / `Marker` / `Rectangle` / `Ellipse` / `Text` / `Mosaic` / `StepBadge` / `Eraser`
  - `Undo` / `Redo`
  - `Pin` 釘選到畫面 (Enter)
  - `Save` 存檔 (Ctrl+S / macOS 為 Cmd+S)
  - `Copy` 複製 (Ctrl+C / macOS 為 Cmd+C)
  - `Cancel` 取消 (Esc)
  - `OCR` 文字辨識（macOS/Windows，支援繁體中文、簡體中文、英文）
  - `Record` 螢幕錄影（`R`）使用選取區域
  - 顏色/線寬控制（支援的工具）
- **螢幕畫布**：
  - 全螢幕標註模式，直接在螢幕上繪圖
  - 繪圖工具：`Pencil` / `Marker` / `Arrow` / `Rectangle` / `Ellipse`
  - 簡報工具：`Laser Pointer` / `Cursor Highlight`（點擊波紋）
  - 顏色/線寬控制
  - 支援 Undo/Redo/Clear
  - `Esc` 離開
- **螢幕錄影**：
  - 從截圖工具列啟動（`Record` 或 `R`）
  - 可調整錄影區域，Start/Cancel 開始或取消
  - 浮動控制列：Pause/Resume/Stop/Cancel
  - FFmpeg 輸出 MP4 (H.264) 或 GIF
- **釘選視窗**：
  - 無邊框、永遠在最上層
  - 可拖曳移動
  - 滑鼠滾輪縮放（顯示縮放比例指示器）
  - Ctrl + 滾輪調整透明度（顯示指示）
  - 邊緣拖曳調整大小
  - 鍵盤旋轉/翻轉：`1` 順時針旋轉、`2` 逆時針旋轉、`3` 水平翻轉、`4` 垂直翻轉
  - 點擊穿透模式：按 `T` 切換（視窗有焦點時），或使用 `Shift+T` 全域熱鍵切換游標下方視窗
  - 雙擊或 Esc 關閉
  - 右鍵選單：複製/存檔/OCR/浮水印/關閉
- **設定對話框**：
  - General 分頁：開機自動啟動
  - Hotkeys 分頁：區域截圖與螢幕畫布分別設定熱鍵
  - Watermark 分頁：文字/圖片浮水印、透明度、位置、縮放
  - Recording 分頁：幀率、輸出格式、儲存位置、自動儲存、FFmpeg 狀態
  - 設定儲存於系統設定 (QSettings)

## 技術棧

- **語言**: C++17
- **框架**: Qt 6（Widgets/Gui/Svg）
- **建置系統**: CMake 3.16+
- **相依套件**: [QHotkey](https://github.com/Skycoder42/QHotkey)（FetchContent 自動取得）、FFmpeg（外部依賴，用於錄影）
- **macOS 原生框架**:
  - CoreGraphics / ApplicationServices（視窗偵測）
  - AppKit（系統整合）
  - Vision（OCR）
  - ScreenCaptureKit（錄影，macOS 12.3+）
  - CoreMedia / CoreVideo（錄影管線）
  - ServiceManagement（自動啟動）
- **Windows API**:
  - Desktop Duplication（DXGI/D3D11，用於錄影）
  - Windows.Media.Ocr（WinRT OCR）

## 系統需求

目前僅支援 macOS 與 Windows。

### macOS
- macOS 10.15+（ScreenCaptureKit 錄影需 12.3+）
- Qt 6（建議以 Homebrew 安裝）
- Xcode Command Line Tools
- CMake 3.16+
- Git（用於 FetchContent 取得 QHotkey）
- FFmpeg（螢幕錄影必需）

### Windows
- Windows 10+
- Qt 6
- Visual Studio 2019+ 或 MinGW
- CMake 3.16+
- Git（用於 FetchContent 取得 QHotkey）
- FFmpeg（螢幕錄影必需，請確保 `ffmpeg.exe` 在 PATH 或 `C:\ffmpeg\bin`）

## 建置與執行

### 開發版本（Debug）

**macOS：**
```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
open build/SnapTray.app
```

**Windows：**
```batch
# 步驟 1：設定（請替換為你的 Qt 路徑）
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.x/msvc2022_64

# 步驟 2：建置
cmake --build build

# 步驟 3：部署 Qt 相依套件（執行程式所需）
# 請替換為你的 Qt 安裝路徑
C:\Qt\6.x\msvc2022_64\bin\windeployqt.exe build\SnapTray.exe

# 步驟 4：執行
build\SnapTray.exe
```

**注意：** Windows 開發版本需要執行 `windeployqt` 來複製 Qt 執行時期 DLL 檔案（Qt6Core.dll、qwindows.dll 平台外掛等）到執行檔旁。此步驟在打包腳本中已針對正式版本自動化。

### 正式版本（Release）

**macOS：**
```bash
cmake -S . -B release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build release
# 產物：release/SnapTray.app
```

**Windows：**
```batch
cmake -S . -B release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.x/msvc2022_64
cmake --build release --config Release

# 部署 Qt 相依套件
C:\Qt\6.x\msvc2022_64\bin\windeployqt.exe --release release\Release\SnapTray.exe

# 產物：release\Release\SnapTray.exe
```

**注意：** 若要發佈，請使用打包腳本（見下方），其已自動化部署流程。

### 打包安裝檔

打包腳本會自動執行 Release 建置、部署 Qt 依賴、並產生安裝檔。

**macOS（DMG）：**
```bash
# 前置需求：brew install qt（如尚未安裝）
# 可選：brew install create-dmg（產生更美觀的 DMG）

./packaging/macos/package.sh
# 輸出：dist/SnapTray-<version>-macOS.dmg
```

**Windows（NSIS 安裝程式）：**
```batch
REM 前置需求：
REM   - Qt 6: https://www.qt.io/download-qt-installer
REM   - NSIS: winget install NSIS.NSIS
REM   - Visual Studio Build Tools 或 Visual Studio

REM 設定 Qt 路徑（如果不是預設位置）
set QT_PATH=C:\Qt\6.x\msvc2022_64

packaging\windows\package.bat
REM 輸出：dist\SnapTray-<version>-Setup.exe
```

#### Code Signing（可選）

未簽章的安裝檔在使用者安裝時會顯示警告：
- macOS：「無法驗證開發者」（需右鍵 → 打開）
- Windows：SmartScreen 警告

**macOS 簽章與公證：**
```bash
# 需要 Apple Developer Program 會員資格（$99 USD/年）
export CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
export NOTARIZE_APPLE_ID="your@email.com"
export NOTARIZE_TEAM_ID="YOURTEAMID"
export NOTARIZE_PASSWORD="xxxx-xxxx-xxxx-xxxx"  # App-Specific Password
./packaging/macos/package.sh
```

**Windows 簽章：**
```batch
REM 需要 Code Signing Certificate（向 DigiCert、Sectigo 等 CA 購買）
set CODESIGN_CERT=path\to\certificate.pfx
set CODESIGN_PASSWORD=your-password
packaging\windows\package.bat
```

## 使用方式

1. 啟動後托盤會出現綠色方塊圖示。
2. 按下區域截圖熱鍵（預設 `F2`）進入截圖模式；或按下螢幕畫布熱鍵（預設 `Ctrl+F2`）進入螢幕畫布模式。
3. **截圖模式操作**：
   - 拖曳滑鼠選取區域
   - 單擊可快速選取偵測到的視窗（macOS/Windows）
   - `Shift`：切換放大鏡中的顏色格式 (HEX/RGB)
   - `C`：複製當前游標下的顏色代碼（尚未放開滑鼠、未完成選取時）
   - 放開滑鼠後出現工具列
4. **工具列操作**：
   - `Enter` 或 `Pin`：將截圖釘選為浮動視窗
   - `Ctrl+C` (Windows) / `Cmd+C` (macOS) 或 `Copy`：複製到剪貼簿
   - `Ctrl+S` (Windows) / `Cmd+S` (macOS) 或 `Save`：儲存成檔案
   - `R` 或 `Record`：開始錄影（可調整區域後按 Start Recording / Enter）
   - `OCR`（macOS/Windows）：辨識選取區內的文字並複製到剪貼簿
   - `Undo/Redo`：`Ctrl+Z` / `Ctrl+Shift+Z`（macOS 通常為 `Cmd+Z` / `Cmd+Shift+Z`）
   - 標註：選擇工具後在選取區內拖曳繪製
     - `Text`：點擊後輸入文字
     - `StepBadge`：點擊放置自動編號的步驟標記
     - `Mosaic`：拖曳筆刷進行馬賽克塗抹
     - `Eraser`：拖曳清除標註
5. **螢幕錄影**：
   - 使用控制列進行 Pause/Resume/Stop/Cancel
6. **螢幕畫布模式操作**：
   - 工具列提供繪圖與簡報工具
   - 點擊顏色/線寬控制調整
   - `Undo` / `Redo`：復原/重做標註
   - `Clear`：清除所有標註
   - `Esc` 或 `Exit`：離開螢幕畫布模式
7. **釘選視窗操作**：
   - 拖曳移動
   - 滑鼠滾輪縮放
   - Ctrl + 滾輪調整透明度
   - 邊緣拖曳調整大小
   - `1` 順時針旋轉、`2` 逆時針旋轉、`3` 水平翻轉、`4` 垂直翻轉
   - 右鍵選單（複製/存檔/OCR/浮水印/關閉）
   - 雙擊或 `Esc` 關閉

## 疑難排解

### 錄影：找不到 FFmpeg

如果你看到類似以下錯誤：
- "FFmpeg not found. Please install FFmpeg to use screen recording."

**解決方法：** 安裝 FFmpeg 並確保在 PATH 中（或放在 macOS 的 `/opt/homebrew/bin/ffmpeg`、Windows 的 `C:\ffmpeg\bin\ffmpeg.exe`）。

### Windows：應用程式無法啟動或顯示缺少 DLL 錯誤

如果你看到類似以下錯誤：
- "The code execution cannot proceed because Qt6Core.dll was not found"
- "This application failed to start because no Qt platform plugin could be initialized"

**解決方法：** 執行 windeployqt 來部署 Qt 相依套件：
```batch
C:\Qt\6.x\msvc2022_64\bin\windeployqt.exe build\SnapTray.exe
```

請將 `C:\Qt\6.x\msvc2022_64` 替換為你實際的 Qt 安裝路徑（應與設定時使用的 CMAKE_PREFIX_PATH 相同）。

## macOS 權限

首次截圖或錄影時系統會要求「螢幕錄製」權限：`系統偏好設定 → 隱私權與安全性 → 螢幕錄製` 勾選 SnapTray，必要時重啟 App。

若要使用視窗偵測功能，需要「輔助使用」權限：`系統偏好設定 → 隱私權與安全性 → 輔助使用` 勾選 SnapTray。

## 專案結構

```
snap-tray/
|-- CMakeLists.txt
|-- README.md
|-- README.zh-TW.md
|-- cmake/
|   |-- Info.plist.in
|   |-- snaptray.rc.in
|   `-- version.h.in
|-- include/
|   |-- MainApplication.h
|   |-- SettingsDialog.h
|   |-- CaptureManager.h
|   |-- RegionSelector.h
|   |-- ScreenCanvas.h
|   |-- PinWindow.h
|   |-- RecordingManager.h
|   |-- WatermarkRenderer.h
|   |-- FFmpegEncoder.h
|   |-- ...
|   `-- capture/
|       |-- ICaptureEngine.h
|       |-- QtCaptureEngine.h
|       |-- SCKCaptureEngine.h
|       `-- DXGICaptureEngine.h
|-- src/
|   |-- main.cpp
|   |-- MainApplication.cpp
|   |-- SettingsDialog.cpp
|   |-- CaptureManager.cpp
|   |-- RegionSelector.cpp
|   |-- ScreenCanvas.cpp
|   |-- PinWindow.cpp
|   |-- RecordingManager.cpp
|   |-- WatermarkRenderer.cpp
|   |-- FFmpegEncoder.cpp
|   |-- ...
|   |-- capture/
|   |   |-- ICaptureEngine.cpp
|   |   |-- QtCaptureEngine.cpp
|   |   |-- SCKCaptureEngine_mac.mm
|   |   `-- DXGICaptureEngine_win.cpp
|   `-- platform/
|       |-- WindowLevel_mac.mm
|       |-- WindowLevel_win.cpp
|       |-- PlatformFeatures_mac.mm
|       `-- PlatformFeatures_win.cpp
|-- resources/
|   |-- resources.qrc
|   `-- icons/
|       |-- snaptray.svg
|       |-- snaptray.png
|       |-- snaptray.icns
|       `-- snaptray.ico
`-- packaging/
    |-- macos/
    |   |-- package.sh
    |   `-- entitlements.plist
    `-- windows/
        |-- package.bat
        |-- installer.nsi
        `-- license.txt
```

## 自訂應用程式圖示

如需更換圖示，請準備 1024x1024 的 PNG 檔案，然後執行：

**macOS (.icns)：**
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

**Windows (.ico)：**
```bash
# 需要 ImageMagick: brew install imagemagick
magick snaptray.png -define icon:auto-resize=256,128,64,48,32,16 snaptray.ico
```

## 已知限制

- 多螢幕支援：截圖會在游標所在螢幕啟動，但不同螢幕 DPI/縮放仍需要更多實機測試。
- 螢幕錄影在無法使用原生 API（ScreenCaptureKit/DXGI）時會改用 Qt 擷取，效能可能較慢。
- 視窗偵測與 OCR 功能支援 macOS 與 Windows。

## 授權條款

MIT License
