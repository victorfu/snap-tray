# SnapTray - 系統托盤截圖與錄影工具

[English](README.md) | **繁體中文**

SnapTray 是一個在系統托盤常駐的截圖與錄影小工具，提供區域截圖、螢幕標註與快速錄影。預設以 F2 進入區域截圖，Ctrl+F2 開啟螢幕畫布。

## 功能特色

- **系統托盤選單**：`Region Capture` (顯示當前熱鍵)、`Screen Canvas` (顯示當前熱鍵)、`Pin from Image...`、`Pin History`、`Close All Pins`、`Record Full Screen`、`Settings`、`Exit`
- **全域快捷鍵**：可於設定中自定義，支援即時更新熱鍵註冊。
  - 區域截圖：預設 `F2`
  - 螢幕畫布：預設 `Ctrl+F2`
- **區域截圖覆蓋層**：
  - 十字線＋放大鏡（支援像素級檢視）
  - RGB/HEX 顏色預覽（按 Shift 切換，按 C 複製顏色代碼）
  - 尺寸標示
  - 選取框控制點（類 Snipaste 風格）
  - 比例鎖定（按住 Shift 限制比例）
  - 多區域選取（可擷取多個區域並合併或分開儲存）
  - 截圖包含游標選項
  - 視窗偵測（macOS/Windows）：自動偵測游標下的視窗，單擊快速選取
  - 右鍵取消選取
- **截圖工具列**：
  - `Selection` 選取工具（調整選取區域）
  - 標註工具：`Arrow` / `Pencil` / `Marker` / `Shape`（Rectangle/Ellipse，外框/填滿）/ `Text` / `Mosaic` / `StepBadge` / `EmojiSticker` / `Eraser`
  - `Undo` / `Redo`
  - `Pin` 釘選到畫面 (Enter)
  - `Save` 存檔 (Ctrl+S / macOS 為 Cmd+S)
  - `Copy` 複製 (Ctrl+C / macOS 為 Cmd+C)
  - `Cancel` 取消 (Esc)
  - `OCR` 文字辨識（macOS/Windows，支援繁體中文、簡體中文、英文）
  - `QR Code Scan`（掃描選取區域中的 QR/條碼）
  - `Auto Blur` 自動偵測並模糊臉孔/文字
  - `Record` 螢幕錄影（`R`）使用選取區域
  - `Multi-Region` 多區域擷取切換（`M`）
  - 顏色/線寬控制（支援的工具）
  - 文字工具格式控制（字型/大小、粗體/斜體/底線）
- **螢幕畫布**：
  - 全螢幕標註模式，直接在螢幕上繪圖
  - 繪圖工具：`Pencil` / `Marker` / `Arrow` / `Shape` / `StepBadge` / `Text` / `EmojiSticker`
  - 簡報工具：`Laser Pointer`
  - 背景模式：`Whiteboard` / `Blackboard`
  - 顏色/線寬控制
  - 支援 Undo/Redo/Clear
  - `Esc` 離開
- **螢幕錄影**：
  - 從截圖工具列（`Record` 或 `R`）或托盤選單（`Record Full Screen`）啟動
  - 可調整錄影區域，Start/Cancel 開始或取消
  - 浮動控制列：Pause/Resume/Stop/Cancel
  - MP4 (H.264) 由原生編碼器（Media Foundation/AVFoundation）產生；GIF 與 WebP 由內建編碼器產生
  - 可選音訊錄製（麥克風/系統音/混合，視平台支援）
  - 錄影包含游標選項
  - 聚光燈效果（錄影時調暗周圍，突顯焦點區域）
- **釘選視窗**：
  - 無邊框、永遠在最上層
  - 可拖曳移動
  - 滑鼠滾輪縮放（顯示縮放比例指示器）
  - Ctrl + 滾輪調整透明度（顯示指示）
  - 邊緣拖曳調整大小
  - 鍵盤旋轉/翻轉：`1` 順時針旋轉、`2` 逆時針旋轉、`3` 水平翻轉、`4` 垂直翻轉
  - 雙擊或 Esc 關閉
  - 右鍵選單：複製/存檔/開啟快取資料夾/OCR/QR Code Scan/浮水印/Click-through/Live Update/關閉
  - **標註工具列**：點擊鉛筆圖示開啟標註工具
    - 繪圖工具：`Pencil` / `Marker` / `Arrow` / `Shape` / `Text` / `Mosaic` / `StepBadge` / `EmojiSticker` / `Eraser`
    - 支援 `Undo` / `Redo`
    - `OCR` / `Copy` / `Save` 快捷操作
- **設定對話框**：
  - General 分頁：開機自動啟動、工具列樣式（深色/淺色）、釘選視窗透明度/縮放/快取設定、CLI 安裝/移除
  - Hotkeys 分頁：區域截圖與螢幕畫布分別設定熱鍵
  - Advanced 分頁：Auto Blur 進階設定
  - Watermark 分頁：圖片浮水印、透明度、位置、縮放
  - OCR 分頁：OCR 語言與辨識後行為設定
  - Recording 分頁：幀率、輸出格式（MP4/GIF/WebP）、品質、倒數計時、點擊高亮、游標顯示、聚光燈、音訊（啟用/來源/裝置）
  - Files 分頁：截圖/錄影儲存路徑、檔名格式
  - Updates 分頁：自動檢查、檢查頻率、預覽版通道、立即檢查
  - About 分頁：版本資訊
  - 背景會定期自動檢查更新
  - 設定儲存於系統設定 (QSettings)

## 技術棧

- **語言**: C++17
- **框架**: Qt 6（Widgets/Gui/Svg/Concurrent/Network）
- **建置系統**: CMake 3.16+ + Ninja
- **相依套件**（以 FetchContent 自動取得）:
  - [QHotkey](https://github.com/Skycoder42/QHotkey)（全域熱鍵）
  - [OpenCV 4.10.0](https://github.com/opencv/opencv)（臉部/文字偵測管線）
  - [libwebp 1.3.2](https://github.com/webmproject/libwebp)（WebP 動畫編碼）
  - [ZXing-CPP v2.2.1](https://github.com/zxing-cpp/zxing-cpp)（QR/條碼處理）
- **macOS 原生框架**:
  - CoreGraphics / ApplicationServices（視窗偵測）
  - AppKit（系統整合）
  - Vision（OCR）
  - AVFoundation（MP4 編碼）
  - ScreenCaptureKit（錄影，macOS 12.3+）
  - CoreMedia / CoreVideo（錄影管線）
  - ServiceManagement（自動啟動）
- **Windows API**:
  - Desktop Duplication（DXGI/D3D11，用於錄影）
  - Media Foundation（MP4 編碼）
  - WASAPI（音訊擷取）
  - Windows.Media.Ocr（WinRT OCR）

## 系統需求

目前僅支援 macOS 與 Windows。

### macOS

- macOS 14.0+
- Qt 6.10.1（Widgets/Gui/Svg/Concurrent/Network，建議以 Homebrew 安裝）
- Xcode Command Line Tools
- CMake 3.16+ 與 Ninja
- Git（用於 FetchContent 相依套件）

### Windows

- Windows 10+
- Qt 6.10.1（MSVC 2022 x64）
- Visual Studio 2022 Build Tools + Windows SDK
- CMake 3.16+ 與 Ninja
- Git（用於 FetchContent 相依套件）

**PowerShell 使用 MSVC**：若使用 PowerShell，需先載入 Visual Studio 環境：

```powershell
Import-Module "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" -DevCmdArguments '-arch=x64' -SkipAutomaticLocation
```

## 建置與執行

### 建議使用腳本

日常開發與驗證建議直接使用 `scripts/` 內建腳本。

**macOS：**

```bash
./scripts/build.sh                  # Debug 建置
./scripts/build-release.sh          # Release 建置
./scripts/run-tests.sh              # 建置並執行測試
./scripts/build-and-run.sh          # Debug 建置 + 執行
./scripts/build-and-run-release.sh  # Release 建置 + 執行
```

**Windows（cmd.exe 或已載入 MSVC 環境的 PowerShell）：**

```batch
scripts\build.bat                   REM Debug 建置
scripts\build-release.bat           REM Release 建置
scripts\run-tests.bat               REM 建置並執行測試
scripts\build-and-run.bat           REM Debug 建置 + 執行
scripts\build-and-run-release.bat   REM Release 建置 + 執行
```

### 手動 CMake（進階）

僅在需要自訂 configure 參數時使用。

**macOS（Debug）：**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
# 產物：build/bin/SnapTray-Debug.app
```

**macOS（Release）：**

```bash
cmake -S . -B release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build release --parallel
# 產物：release/bin/SnapTray.app
```

**Windows（Debug）：**

```batch
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/msvc2022_64
cmake --build build --parallel
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

**Windows（Release）：**

```batch
cmake -S . -B release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/msvc2022_64
cmake --build release --parallel
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe --release release\bin\SnapTray.exe
```

### 打包安裝檔

打包腳本會自動執行 Release 建置、部署 Qt 依賴、並產生安裝檔。

**macOS（DMG）：**

```bash
# 前置需求：brew install qt（如尚未安裝）
# 可選：brew install create-dmg（產生更美觀的 DMG）

./packaging/macos/package.sh
# 輸出：dist/SnapTray-<version>-macOS.dmg
```

**Windows：**

```batch
REM 前置需求：
REM   - Qt 6: https://www.qt.io/download-qt-installer
REM   - NSIS: winget install NSIS.NSIS（用於 NSIS 安裝程式）
REM   - Windows 10 SDK：Visual Studio 已包含（用於 MSIX 套件）
REM   - Visual Studio Build Tools 或 Visual Studio

REM 設定 Qt 路徑（如果不是預設位置）
set QT_PATH=C:\Qt\6.10.1\msvc2022_64

packaging\windows\package.bat           REM 建置 NSIS 和 MSIX
packaging\windows\package.bat nsis      REM 僅建置 NSIS 安裝程式
packaging\windows\package.bat msix      REM 僅建置 MSIX 套件

REM 輸出：
REM   dist\SnapTray-<version>-Setup.exe     (NSIS 安裝程式)
REM   dist\SnapTray-<version>.msix          (MSIX 套件)
REM   dist\SnapTray-<version>.msixupload    (用於 Store 提交)
```

**MSIX 本地安裝（測試用）：**

在本地安裝未簽章的 MSIX 套件進行測試：

```powershell
Add-AppPackage -Path "dist\SnapTray-1.0.7.msix" -AllowUnsigned
```

解除安裝：

```powershell
Get-AppPackage SnapTray* | Remove-AppPackage
```

**Microsoft Store 提交：**

1. 登入 [Partner Center](https://partner.microsoft.com)
2. 建立新的應用程式保留或選擇現有應用程式
3. 建立新的提交
4. 上傳 `.msixupload` 檔案
5. 完成 Store 清單、定價與年齡分級
6. 提交認證

> **注意：** Store 提交需要 Microsoft Partner Center 帳戶。套件會在提交過程中由 Microsoft 自動簽章。

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
REM NSIS 安裝程式簽章（需要 Code Signing Certificate）
set CODESIGN_CERT=path\to\certificate.pfx
set CODESIGN_PASSWORD=your-password
packaging\windows\package.bat nsis

REM MSIX 簽章（用於側載/企業部署）
set MSIX_SIGN_CERT=path\to\certificate.pfx
set MSIX_SIGN_PASSWORD=your-password
set PUBLISHER_ID=CN=SnapTray Dev
packaging\windows\package.bat msix
```

> **注意：** 若要上架 Microsoft Store，MSIX 不需要簽章 - Microsoft 會在認證過程中自動簽章套件。

## 建置優化

建置系統支援編譯器快取，可加速增量建置。

### 本地開發

安裝編譯器快取工具可大幅加速重新建置：

**macOS：**

```bash
brew install ccache
```

**Windows：**

```bash
scoop install sccache
```

CMake 會自動偵測並使用編譯器快取，無需額外設定。

### 效能提升

| 優化項目                | 位置           | 效果              |
| ----------------------- | -------------- | ----------------- |
| ccache/sccache 自動偵測 | CMakeLists.txt | 增量建置快 50-90% |
| CI ccache (macOS)       | ci.yml         | CI 快 30-50%      |
| CI sccache (Windows)    | ci.yml         | CI 快 30-50%      |

**注意：** 由於與 macOS 上的 Objective-C++ 檔案不相容，未使用預編譯標頭 (PCH) 與 Unity Build。

## 使用方式

1. 啟動後托盤會出現 SnapTray 圖示。
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
   - `QR Code Scan`：偵測並解碼選取區中的 QR/條碼
   - `M` 或 `Multi-Region`：切換多區域擷取模式
   - `Undo/Redo`：`Ctrl+Z` / `Ctrl+Shift+Z`（macOS 通常為 `Cmd+Z` / `Cmd+Shift+Z`）
   - 標註：選擇工具後在選取區內拖曳繪製
     - `Text`：點擊後輸入文字
     - `StepBadge`：點擊放置自動編號的步驟標記
     - `Mosaic`：拖曳筆刷進行馬賽克塗抹
     - `Eraser`：拖曳清除標註
5. **螢幕錄影**：
   - 使用控制列進行 Pause/Resume/Stop/Cancel
6. **螢幕畫布模式操作**：
   - 工具列提供繪圖工具與雷射筆
   - 使用 `Whiteboard` / `Blackboard` 按鈕切換畫布背景模式
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
   - 右鍵選單（複製/存檔/開啟快取資料夾/OCR/QR Code Scan/浮水印/Click-through/Live Update/關閉）
   - 雙擊或 `Esc` 關閉
   - 點擊鉛筆圖示開啟標註工具列，可在釘選圖片上繪圖

## 命令列介面 (CLI)

SnapTray 提供 CLI 介面，可用於腳本自動化。

### CLI 安裝設定

**macOS：**
開啟 Settings → General → Install CLI 以建立系統級的 `snaptray` 指令。此操作需要管理員權限。

**Windows (NSIS 安裝程式)：**
安裝程式會自動將 SnapTray 加入系統 PATH。安裝後請開啟新的終端機視窗。

**Windows (MSIX/MS Store)：**
透過 App Execution Alias，`snaptray` 指令在安裝後立即可用。

### CLI 指令

| 指令 | 說明 | 需要主程式運行 |
|------|------|--------------|
| `full` | 擷取全螢幕 | 否 |
| `screen` | 擷取指定螢幕 | 否 |
| `region` | 擷取指定區域 | 否 |
| `gui` | 開啟區域截圖 GUI | 是 |
| `canvas` | 切換螢幕畫布 | 是 |
| `record` | 開始/停止/切換錄影 | 是 |
| `pin` | 釘選圖片 | 是 |
| `config` | 檢視/修改設定 | 部分 |

### CLI 範例

```bash
# 說明與版本
snaptray --help
snaptray --version
snaptray full --help

# 本地擷取指令（不需要主程式運行）
snaptray full -c                      # 全螢幕到剪貼簿
snaptray full -d 1000 -o shot.png     # 延遲 1 秒後儲存
snaptray full -o screenshot.png       # 全螢幕到檔案
snaptray screen --list                # 列出可用螢幕
snaptray screen 0 -c                  # 擷取螢幕 0（位置參數寫法）
snaptray screen -n 1 -o screen1.png   # 擷取螢幕 1（選項寫法）
snaptray screen 1 -o screen1.png      # 擷取螢幕 1 到檔案
snaptray region -r 0,0,800,600 -c     # 在螢幕 0 擷取區域到剪貼簿
snaptray region -n 1 -r 100,100,400,300 -o region.png
snaptray region -r 100,100,400,300 -o region.png

# IPC 指令（需要主程式運行）
snaptray gui                          # 開啟區域截圖選擇器
snaptray gui -d 2000                  # 延遲 2 秒後開啟
snaptray canvas                       # 切換螢幕畫布模式
snaptray record start                 # 開始錄影
snaptray record stop                  # 停止錄影
snaptray record                       # 切換錄影
snaptray record start -n 1            # 在螢幕 1 開始全螢幕錄影
snaptray pin -f image.png             # 釘選圖片檔案
snaptray pin -c --center              # 從剪貼簿釘選並置中
snaptray pin -f image.png -x 200 -y 120

# 設定指令
snaptray config --list
snaptray config --get hotkeys/region_capture
snaptray config --set files/filename_prefix SnapTray
snaptray config --reset

# 選項
full/screen/region:
  -c, --clipboard    複製到剪貼簿
  -o, --output       儲存到檔案
  -p, --path         儲存目錄
  -d, --delay        擷取前延遲（毫秒）
  -n, --screen       螢幕編號
  -r, --region       區域座標（僅 region：x,y,寬,高）
  --raw              輸出原始 PNG 到 stdout
  --cursor           包含滑鼠游標

screen 專屬:
  --list             列出可用螢幕

record:
  [action]           start | stop | toggle（預設：toggle）

pin:
  -f, --file         圖片檔路徑
  -c, --clipboard    從剪貼簿釘選
  -x, --pos-x        視窗 X 座標
  -y, --pos-y        視窗 Y 座標
  --center           視窗置中
```

### 回傳碼

| 代碼 | 意義 |
|------|------|
| 0 | 成功 |
| 1 | 一般錯誤 |
| 2 | 無效參數 |
| 3 | 檔案錯誤 |
| 4 | 實例錯誤（主程式未運行） |
| 5 | 錄影錯誤 |

## 疑難排解

### macOS：「SnapTray」無法打開，因為 Apple 無法驗證

如果你看到以下訊息：

- 「SnapTray」無法打開，因為 Apple 無法驗證其不含惡意軟體

**解決方法：** 使用終端機移除隔離屬性：

```bash
xattr -cr /Applications/SnapTray.app
```

這會移除 macOS 對下載應用程式添加的隔離標記。執行此指令後，你應該可以正常開啟 SnapTray。

### Windows：應用程式無法啟動或顯示缺少 DLL 錯誤

如果你看到類似以下錯誤：

- "The code execution cannot proceed because Qt6Core.dll was not found"
- "This application failed to start because no Qt platform plugin could be initialized"

**解決方法：** 執行 windeployqt 來部署 Qt 相依套件：

```batch
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

請將 `C:\Qt\6.10.1\msvc2022_64` 替換為你實際的 Qt 安裝路徑（應與設定時使用的 CMAKE_PREFIX_PATH 相同）。

## macOS 權限

首次截圖或錄影時系統會要求「螢幕錄製」權限：`系統設定 → 隱私權與安全性 → 螢幕錄製` 勾選 SnapTray，必要時重啟 App。

若要使用視窗偵測功能，需要「輔助使用」權限：`系統設定 → 隱私權與安全性 → 輔助使用` 勾選 SnapTray。

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
|   |-- annotation/        # 標註上下文與主機介面
|   |-- annotations/       # 標註類型（Arrow、Shape、Text 等）
|   |-- capture/           # 螢幕/音訊擷取引擎
|   |-- cli/               # CLI 處理器、指令、IPC 協定
|   |-- colorwidgets/      # 自訂色彩選擇器元件
|   |-- cursor/            # 游標狀態管理
|   |-- detection/         # 臉部/文字偵測
|   |-- encoding/          # GIF/WebP 編碼器
|   |-- external/          # 第三方標頭檔（msf_gif）
|   |-- hotkey/            # 熱鍵管理器與型別定義
|   |-- input/             # 滑鼠點擊追蹤
|   |-- pinwindow/         # 釘選視窗元件
|   |-- recording/         # 錄影特效（Spotlight、CursorHighlight）
|   |-- region/            # 區域選取元件
|   |-- settings/          # 設定管理器
|   |-- toolbar/           # 工具列繪製
|   |-- tools/             # 工具處理器與註冊
|   |-- ui/                # GlobalToast、IWidgetSection
|   |   `-- sections/      # 工具選項面板
|   |-- update/            # 自動更新系統
|   |-- utils/             # 工具輔助函式
|   |-- video/             # 影片播放與錄影 UI
|   `-- widgets/           # 自訂 Widget（熱鍵編輯、對話框）
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
|   |-- annotation/        # AnnotationContext 實作
|   |-- annotations/       # 標註實作
|   |-- capture/           # 擷取引擎實作
|   |-- cli/               # CLI 指令實作
|   |-- colorwidgets/      # 色彩元件實作
|   |-- cursor/            # CursorManager 實作
|   |-- detection/         # 偵測實作
|   |-- encoding/          # 編碼器實作
|   |-- hotkey/            # HotkeyManager 實作
|   |-- input/             # 輸入追蹤（平台特定）
|   |-- pinwindow/         # 釘選視窗元件實作
|   |-- platform/          # 平台抽象層（macOS/Windows）
|   |-- recording/         # 錄影特效實作
|   |-- region/            # 區域選取實作
|   |-- settings/          # 設定管理器實作
|   |-- toolbar/           # 工具列實作
|   |-- tools/             # 工具系統實作
|   |-- ui/sections/       # UI 區段實作
|   |-- update/            # 自動更新實作
|   |-- utils/             # 工具函式實作
|   |-- video/             # 影片元件實作
|   `-- widgets/           # Widget 實作
|-- resources/
|   |-- resources.qrc
|   |-- icons/
|   |   |-- snaptray.svg
|   |   |-- snaptray.png
|   |   |-- snaptray.icns
|   |   `-- snaptray.ico
|   `-- cascades/          # OpenCV Haar cascade 檔案
|-- scripts/
|   |-- build.sh / build.bat
|   |-- build-release.sh / build-release.bat
|   |-- build-and-run.sh / build-and-run.bat
|   |-- build-and-run-release.sh / build-and-run-release.bat
|   `-- run-tests.sh / run-tests.bat
|-- tests/
|   |-- Annotations/
|   |-- CLI/
|   |-- Detection/
|   |-- Encoding/
|   |-- Hotkey/
|   |-- IPC/
|   |-- PinWindow/
|   |-- RecordingManager/
|   |-- RegionSelector/
|   |-- Settings/
|   |-- ToolOptionsPanel/
|   |-- Tools/
|   |-- UISections/
|   |-- Update/
|   |-- Utils/
|   `-- Video/
`-- packaging/
    |-- macos/
    |   |-- package.sh
    |   `-- entitlements.plist
    `-- windows/
        |-- package.bat
        |-- package-nsis.bat
        |-- package-msix.bat
        |-- installer.nsi
        |-- license.txt
        |-- AppxManifest.xml.in
        |-- generate-assets.ps1
        `-- assets/
```

## 架構設計

程式碼採用模組化架構，提取獨立組件以提升可維護性：

### 已提取組件

| 組件                        | 位置               | 職責                       |
| --------------------------- | ------------------ | -------------------------- |
| `CursorManager`             | `src/cursor/`      | 集中式游標狀態管理         |
| `MagnifierPanel`            | `src/region/`      | 放大鏡繪製與快取管理       |
| `UpdateThrottler`           | `src/region/`      | 事件節流邏輯               |
| `TextAnnotationEditor`      | `src/region/`      | 文字註釋編輯/變換/格式化   |
| `SelectionStateManager`     | `src/region/`      | 選取狀態與操作管理         |
| `RegionExportManager`       | `src/region/`      | 區域匯出（複製/儲存）處理  |
| `RegionInputHandler`        | `src/region/`      | 區域選取輸入事件處理       |
| `RegionPainter`             | `src/region/`      | 區域繪製邏輯               |
| `MultiRegionManager`        | `src/region/`      | 多區域擷取協調管理         |
| `AnnotationSettingsManager` | `src/settings/`    | 集中式註釋設定管理         |
| `FileSettingsManager`       | `src/settings/`    | 檔案路徑設定               |
| `PinWindowSettingsManager`  | `src/settings/`    | 釘選視窗設定               |
| `AutoBlurSettingsManager`   | `src/settings/`    | 自動模糊功能設定           |
| `WatermarkSettingsManager`  | `src/settings/`    | 浮水印設定                 |
| `ImageTransformer`          | `src/pinwindow/`   | 圖片旋轉/翻轉/縮放         |
| `ResizeHandler`             | `src/pinwindow/`   | 視窗邊緣調整大小           |
| `UIIndicators`              | `src/pinwindow/`   | 縮放/透明度/穿透指示器     |
| `ClickThroughExitButton`    | `src/pinwindow/`   | 穿透模式離開按鈕           |
| `PinHistoryStore`           | `src/pinwindow/`   | 釘選歷史紀錄持久化         |
| `PinHistoryWindow`          | `src/pinwindow/`   | 釘選歷史紀錄 UI            |
| `SpotlightEffect`           | `src/recording/`   | 錄影時聚光燈效果           |
| `CursorHighlightEffect`     | `src/recording/`   | 錄影時游標高亮效果         |
| `FaceDetector`              | `src/detection/`   | 自動模糊臉部偵測           |
| `AutoBlurManager`           | `src/detection/`   | 自動模糊協調管理           |
| `HotkeyManager`             | `src/hotkey/`      | 集中式熱鍵註冊管理         |
| `CLIHandler`                | `src/cli/`         | CLI 指令解析與分派         |
| `UpdateChecker`             | `src/update/`      | 自動更新版本檢查           |
| `UpdateSettingsManager`     | `src/update/`      | 更新偏好設定與排程         |
| `AnnotationContext`         | `src/annotation/`  | 共享標註執行上下文         |
| Color widgets               | `src/colorwidgets/`| 自訂色彩選擇器對話框與元件 |
| Section 類別                | `src/ui/sections/` | 工具選項面板組件           |

### 測試套件

目前 Qt Test 測試涵蓋：

- Tool options 與 UI sections
- Region selector 與 Pin window 行為
- Recording lifecycle 與整合流程
- Encoding 與 Detection 模組
- CLI/IPC 流程
- Settings、Hotkeys、Update 元件與工具函式
- Video timeline 元件

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
- macOS 系統音錄製需 macOS 13+（Ventura）或虛擬音訊裝置（如 BlackHole）。

## 授權條款

MIT License
