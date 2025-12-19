# SnapTray - 系統托盤截圖工具

SnapTray 是一個在系統托盤常駐的區域截圖小工具，預設以 F2 觸發選取，並可將截圖釘選在螢幕上、複製或儲存。

## 功能特色

- **系統托盤選單**：`Region Capture` (顯示當前熱鍵)、`Close All Pins`、`Settings`、`Exit`
- **全域快捷鍵**：可於設定中自定義，預設為 `F2`。支援即時更新熱鍵註冊。
- **區域截圖覆蓋層**：
  - 十字線＋放大鏡（支援像素級檢視）
  - RGB/HEX 顏色預覽（按 Shift 切換，按 C 複製顏色代碼）
  - 尺寸標示
  - 選取框控制點（類 Snipaste 風格）
  - 視窗偵測（macOS）：自動偵測游標下的視窗，單擊快速選取
- **截圖工具列**：
  - `Selection` 選取工具（調整選取區域）
  - `Pin` 釘選到畫面 (Enter)
  - `Save` 存檔 (Ctrl+S)
  - `Copy` 複製 (Ctrl+C)
  - `Cancel` 取消 (Esc)
  - 標註工具：`Arrow` / `Pencil` / `Marker` / `Rectangle` / `Text` / `Mosaic` / `StepBadge`（支援 Undo/Redo）
  - `OCR` 文字辨識（僅 macOS，支援繁體中文、簡體中文、英文）
- **釘選視窗**：
  - 無邊框、永遠在最上層
  - 可拖曳移動
  - 滑鼠滾輪縮放（顯示縮放比例指示器）
  - 邊緣拖曳調整大小
  - 旋轉支援（右鍵選單）
  - 雙擊或 Esc 關閉
  - 右鍵選單：存檔/複製/OCR/縮放/旋轉/關閉
- **設定對話框**：可自定義全域熱鍵並儲存於系統設定 (QSettings)。

## 技術棧

- **語言**: C++17
- **框架**: Qt 6（Widgets/Gui）
- **建置系統**: CMake 3.16+
- **相依套件**: [QHotkey](https://github.com/Skycoder42/QHotkey)（FetchContent 自動取得）
- **macOS 原生框架**:
  - CoreGraphics / ApplicationServices（視窗偵測）
  - AppKit（系統整合）
  - Vision（OCR 文字辨識）

## 系統需求

### macOS
- macOS 10.15+
- Qt 6（建議以 Homebrew 安裝）
- Xcode Command Line Tools
- CMake 3.16+
- Git（用於 FetchContent 取得 QHotkey）

### Windows
- Windows 10+
- Qt 6
- Visual Studio 2019+ 或 MinGW
- CMake 3.16+
- Git（用於 FetchContent 取得 QHotkey）

## 建置與執行

### 開發版本（Debug）

```bash
# macOS
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
# Windows（請替換為你的 Qt 路徑）
# cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.x.x/msvc2019_64

cmake --build build

# 執行（macOS 會產出 .app bundle）
open build/SnapTray.app  # macOS
# build/SnapTray.exe     # Windows
```

### 正式版本（Release）

```bash
cmake -S . -B release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build release
# 產物：release/SnapTray.app（macOS）
```

### 打包安裝檔

打包腳本會自動執行 Release 建置、部署 Qt 依賴、並產生安裝檔。

**macOS（DMG）：**
```bash
# 前置需求：brew install qt（如尚未安裝）
# 可選：brew install create-dmg（產生更美觀的 DMG）

./packaging/macos/package.sh
# 輸出：dist/SnapTray-1.0.0-macOS.dmg
```

**Windows（NSIS 安裝程式）：**
```batch
REM 前置需求：
REM   - Qt 6: https://www.qt.io/download-qt-installer
REM   - NSIS: winget install NSIS.NSIS
REM   - Visual Studio Build Tools 或 Visual Studio

REM 設定 Qt 路徑（如果不是預設位置）
set QT_PATH=C:\Qt\6.6.1\msvc2019_64

packaging\windows\package.bat
REM 輸出：dist\SnapTray-1.0.0-Setup.exe
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
2. 按下全域熱鍵（預設 `F2`）進入截圖模式。
3. **截圖模式操作**：
   - 拖曳滑鼠選取區域
   - 單擊可快速選取偵測到的視窗（macOS）
   - `Shift`：切換放大鏡中的顏色格式 (HEX/RGB)
   - `C`：複製當前游標下的顏色代碼（尚未放開滑鼠、未完成選取時）
   - 放開滑鼠後出現工具列
4. **工具列操作**：
   - `Enter` 或 `Pin`：將截圖釘選為浮動視窗
   - `Ctrl+C` 或 `Copy`：複製到剪貼簿
   - `Ctrl+S` 或 `Save`：儲存成檔案
   - `Esc` 或 `Cancel`：取消選取
   - 標註：選擇工具後在選取區內拖曳繪製
     - `Text`：點擊後輸入文字
     - `StepBadge`：點擊放置自動編號的步驟標記
     - `Mosaic`：拖曳筆刷進行馬賽克塗抹
   - `OCR`（macOS）：辨識選取區內的文字並複製到剪貼簿
   - `Undo/Redo`：`Ctrl+Z` / `Ctrl+Shift+Z`（macOS 通常為 `Cmd+Z` / `Cmd+Shift+Z`）
5. **釘選視窗操作**：
   - 拖曳移動
   - 滑鼠滾輪縮放
   - 邊緣拖曳調整大小
   - 右鍵選單（存檔/複製/OCR/放大/縮小/重設縮放/順時針旋轉/逆時針旋轉/關閉）
   - 雙擊或 `Esc` 關閉

## macOS 權限

首次截圖時系統會要求「螢幕錄製」權限：`系統偏好設定 → 隱私權與安全性 → 螢幕錄製` 勾選 SnapTray，必要時重啟 App。

若要使用視窗偵測功能，需要「輔助使用」權限：`系統偏好設定 → 隱私權與安全性 → 輔助使用` 勾選 SnapTray。

## 專案結構

```
snap/
├── CMakeLists.txt
├── Info.plist
├── README.md
├── include/
│   ├── MainApplication.h
│   ├── SettingsDialog.h
│   ├── CaptureManager.h
│   ├── RegionSelector.h
│   ├── PinWindow.h
│   ├── PinWindowManager.h
│   ├── AnnotationLayer.h
│   ├── WindowDetector.h
│   └── OCRManager.h
├── src/
│   ├── main.cpp
│   ├── MainApplication.cpp
│   ├── SettingsDialog.cpp
│   ├── CaptureManager.cpp
│   ├── RegionSelector.cpp
│   ├── PinWindow.cpp
│   ├── PinWindowManager.cpp
│   ├── AnnotationLayer.cpp
│   ├── WindowDetector.mm        # macOS
│   ├── WindowDetector_win.cpp   # Windows
│   ├── OCRManager.mm            # macOS
│   └── OCRManager_win.cpp       # Windows
├── resources/
│   ├── resources.qrc
│   ├── snaptray.rc              # Windows 資源檔
│   └── icons/
│       ├── snaptray.svg         # 原始圖示
│       ├── snaptray.png         # 1024x1024 PNG
│       ├── snaptray.icns        # macOS 圖示
│       └── snaptray.ico         # Windows 圖示
└── packaging/
    ├── macos/
    │   ├── package.sh           # macOS 打包腳本
    │   └── entitlements.plist   # Code signing 權限
    └── windows/
        ├── package.bat          # Windows 打包腳本
        ├── installer.nsi        # NSIS 安裝程式
        └── license.txt          # 授權文字
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

- 標註功能目前未提供 UI 調整顏色/線寬（例如畫筆預設紅色、線寬固定）。
- 多螢幕支援：截圖會在游標所在螢幕啟動，但不同螢幕 DPI/縮放仍需要更多實機測試。
- 視窗偵測與 OCR 功能僅支援 macOS。

## 授權條款

MIT License
