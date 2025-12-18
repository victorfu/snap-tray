# SnapTray - 系統托盤截圖工具

SnapTray 是一個跨平台（macOS/Windows）的截圖工具，運行於系統托盤，支援全域快捷鍵觸發截圖。

## 功能特色

- 系統托盤常駐運行
- 全域快捷鍵截圖（預設：`Ctrl+Shift+S`）
- 支援多螢幕截圖
- 可自訂快捷鍵設定
- 跨平台支援（macOS / Windows）

## 技術棧

- **語言**: C++17
- **框架**: Qt 6.9+
- **建置系統**: CMake 3.16+
- **相依套件**: [QHotkey](https://github.com/Skycoder42/QHotkey)（透過 FetchContent 自動下載）

## 系統需求

### macOS
- macOS 10.15 或更新版本
- Qt 6.9+（建議透過 Homebrew 安裝）
- Xcode Command Line Tools
- CMake 3.16+

### Windows
- Windows 10 或更新版本
- Qt 6.9+
- Visual Studio 2019+ 或 MinGW
- CMake 3.16+

## 開發環境設定

### macOS

1. **安裝 Homebrew**（如尚未安裝）:
   ```bash
   /bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
   ```

2. **安裝 Qt6**:
   ```bash
   brew install qt
   ```

3. **設定環境變數**（加入 `~/.zshrc` 或 `~/.bash_profile`）:
   ```bash
   export CMAKE_PREFIX_PATH=$(brew --prefix qt)
   ```

4. **重新載入設定**:
   ```bash
   source ~/.zshrc
   ```

### Windows

1. 從 [Qt 官網](https://www.qt.io/download) 下載並安裝 Qt 6.9+
2. 安裝 CMake
3. 設定環境變數 `CMAKE_PREFIX_PATH` 指向 Qt 安裝路徑

## 編譯與執行

### 開發版本（Debug）

```bash
# 建立 build 目錄
mkdir build && cd build

# 設定 CMake（macOS）
cmake .. -DCMAKE_PREFIX_PATH=$(brew --prefix qt)

# 設定 CMake（Windows，請替換為你的 Qt 路徑）
# cmake .. -DCMAKE_PREFIX_PATH=C:/Qt/6.9.3/msvc2019_64

# 編譯
make        # macOS/Linux
# cmake --build . # Windows

# 執行（macOS 會自動產生 .app bundle）
open SnapTray.app  # macOS
# SnapTray.exe     # Windows
```

### 正式版本（Release / Production）

```bash
# 建立獨立的 release 目錄
mkdir release && cd release

# 設定 CMake 為 Release 模式
cmake .. -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=$(brew --prefix qt)

# 編譯
make

# 產生的 .app bundle 位於
# release/SnapTray.app
```

### macOS 應用程式發布（DMG 打包）

編譯完成後，使用 `macdeployqt` 打包所有相依函式庫：

```bash
cd release

# 打包相依函式庫並產生 DMG
$(brew --prefix qt)/bin/macdeployqt SnapTray.app -dmg

# 產生的檔案：
# - SnapTray.app (包含所有相依函式庫的獨立應用程式)
# - SnapTray.dmg (可發布的磁碟映像檔)
```

**macOS 特性說明：**
- 應用程式設定為 `LSUIElement=true`，不會出現在 Dock 中
- 包含 `NSScreenCaptureUsageDescription`，系統會自動顯示權限請求對話框
- 支援 Retina 高解析度顯示 (`NSHighResolutionCapable=true`)

## 專案結構

```
snap/
├── CMakeLists.txt          # CMake 建置設定
├── Info.plist              # macOS 應用程式設定檔
├── README.md               # 本文件
├── include/                # 標頭檔
│   ├── MainApplication.h   # 主應用程式類別
│   └── SettingsDialog.h    # 設定對話框
└── src/                    # 原始碼
    ├── main.cpp            # 程式進入點
    ├── MainApplication.cpp # 主應用程式實作
    └── SettingsDialog.cpp  # 設定對話框實作
```

## 使用方式

1. 執行程式後，會在系統托盤（macOS 選單列 / Windows 系統托盤）出現綠色方塊圖示
2. **右鍵點擊**圖示顯示選單：
   - **Capture**: 立即截圖
   - **Settings**: 開啟設定對話框，可自訂快捷鍵
   - **Exit**: 結束程式
3. 使用**全域快捷鍵**（預設 `Ctrl+Shift+S`）可在任何時候觸發截圖

## macOS 權限設定

首次使用截圖功能時，macOS 會要求授予「螢幕錄製」權限：

1. 開啟「系統偏好設定」→「隱私權與安全性」→「螢幕錄製」
2. 勾選 SnapTray 應用程式
3. 可能需要重新啟動應用程式

## 開發注意事項

- 修改程式碼後，只需在 build 目錄執行 `make` 即可重新編譯
- QHotkey 函式庫會在首次編譯時自動下載
- 若遇到 CMake 快取問題，可刪除 build 目錄重新設定

## 授權條款

MIT License
