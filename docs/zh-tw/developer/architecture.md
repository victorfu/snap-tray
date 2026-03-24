---
layout: docs
title: 架構總覽
description: 整理專案結構、library 邊界、主要 subsystem、平台抽象與貢獻者需要遵守的開發慣例。
permalink: /zh-tw/developer/architecture/
lang: zh-tw
route_key: developer_architecture
nav_data: developer_nav
docs_copy_key: developer
---

## 技術棧

- 語言：C++17
- 框架：Qt 6
- 建置系統：CMake + Ninja
- UI 技術：Qt Widgets 搭配 QML overlays 與 settings surfaces
- 打包目標：macOS DMG、Windows NSIS、Windows MSIX

## 專案結構

```text
snap-tray/
├── include/                    # 公開標頭與 subsystem 介面
│   ├── annotation/             # Annotation context 與 host adapter
│   ├── annotations/            # 各種 annotation item
│   ├── beautify/               # Beautify 介面與設定
│   ├── capture/                # 擷取引擎介面
│   ├── cli/                    # CLI handler、commands、IPC protocol
│   ├── colorwidgets/           # 自訂色彩元件
│   ├── cursor/                 # 游標管理
│   ├── detection/              # Face / table / credential detection
│   ├── encoding/               # Video、GIF、WebP encoders
│   ├── history/                # 歷史資料模型與輔助物件
│   ├── hotkey/                 # Hotkey manager 與 action types
│   ├── mcp/                    # 僅 Debug build 的 MCP server contract
│   ├── metal/                  # Apple Metal capture / rendering helpers
│   ├── pinwindow/              # 釘選視窗元件
│   ├── platform/               # 平台抽象層
│   ├── qml/                    # QML bridges 與 view models
│   ├── region/                 # Region selector 元件
│   ├── settings/               # Settings managers
│   ├── share/                  # Share upload client
│   ├── tools/                  # Tool registry 與 handlers
│   ├── ui/                     # 共用 UI helper 與 theme
│   ├── update/                 # Auto-update 類別
│   ├── utils/                  # 工具函式
│   ├── video/                  # 播放與錄影 UI
│   └── widgets/                # 自訂 widgets 與 dialogs
├── src/                        # 與 include/ 對應的實作
│   ├── qml/components/         # 共用 QML 區塊
│   ├── qml/controls/           # 可重用 control
│   ├── qml/dialogs/            # Dialog surfaces
│   ├── qml/panels/             # Capture overlay 面板
│   ├── qml/recording/          # Recording overlays
│   ├── qml/settings/           # Settings pages
│   ├── qml/tokens/             # QML design tokens
│   └── qml/toolbar/            # Floating toolbar surfaces
├── tests/                      # 依 subsystem 分組的 Qt Test
├── docs/                       # 網站、使用者文件、開發者文件
├── packaging/                  # macOS 與 Windows 打包
├── resources/                  # Icons、cascades、shaders、resources.qrc
├── scripts/                    # build、run、test、i18n 腳本
├── translations/               # 翻譯來源與輸出
└── CMakeLists.txt              # 最上層設定與依賴管理
```

## Library 架構

SnapTray 以模組化 static-library 風格組織：

1. `snaptray_core`：annotations、settings、cursor、utilities、CLI 共用邏輯
2. `snaptray_colorwidgets`：自訂色彩選擇 UI
3. `snaptray_algorithms`：偵測與影像分析
4. `snaptray_platform`：平台特定擷取、編碼、播放、安裝來源與 OS 整合
5. `snaptray_ui`：region selector、pin windows、recording workflow、toolbar、settings surfaces
6. `snaptray_mcp`：僅 Debug build 的 MCP transport 與 tools
7. `SnapTray`：主程式 executable

## 架構模式

### 設定邊界以 settings manager 為主

請優先使用 subsystem 專屬 settings manager，而不是零散直接存取 `QSettings`。

- `AnnotationSettingsManager`
- `FileSettingsManager`
- `PinWindowSettingsManager`
- `AutoBlurSettingsManager`
- `OCRSettingsManager`
- `WatermarkSettingsManager`
- `RecordingSettingsManager`
- `UpdateSettingsManager`
- `BeautifySettingsManager`
- `RegionCaptureSettingsManager`
- `LanguageManager`
- `MCPSettingsManager`

### Hotkey 註冊集中管理

請經由 `HotkeyManager` 處理，不要在 feature code 直接 new `QHotkey`。動作分類由 `HotkeyAction` 管理。

### Tool 系統採資料驅動

工具由 `ToolId`、`ToolDefinition`、handlers 與 `ToolRegistry` 組成。能用 lookup table / capability map，就不要堆大段 `switch`。

### 平台抽象要明確

請透過 `PlatformFeatures` 與 platform-specific implementation layer 存取能力，不要把 OS 判斷灑在 feature code 裡。

### 浮動玻璃面板使用共用 renderer

請優先使用 `GlassRenderer` 與既有 toolbar style config，不要在各 widget 重寫一套面板渲染。

## 平台特定程式碼對照

| 功能 | Windows | macOS |
|---|---|---|
| 螢幕擷取 | `DXGICaptureEngine_win.cpp` | `SCKCaptureEngine_mac.mm` |
| 影片編碼 | `MediaFoundationEncoder.cpp` | `AVFoundationEncoder.mm` |
| 音訊擷取 | `WASAPIAudioCaptureEngine_win.cpp` | `CoreAudioCaptureEngine_mac.mm` |
| 影片播放 | `MediaFoundationPlayer_win.cpp` | `AVFoundationPlayer_mac.mm` |
| 視窗偵測 | `WindowDetector_win.cpp` | `WindowDetector.mm` |
| OCR | `OCRManager_win.cpp` | `OCRManager.mm` |
| Window level | `WindowLevel_win.cpp` | `WindowLevel_mac.mm` |
| Platform features | `PlatformFeatures_win.cpp` | `PlatformFeatures_mac.mm` |
| Auto-launch | `AutoLaunchManager_win.cpp` | `AutoLaunchManager_mac.mm` |
| Install source | `InstallSourceDetector_win.cpp` | `InstallSourceDetector_mac.mm` |
| PATH utilities | `PathEnvUtils_win.cpp` | N/A |
| Image color space | N/A | `ImageColorSpaceHelper_mac.mm` |

## 主要元件

### Core managers

- `CaptureManager`：區域擷取工作流入口
- `PinWindowManager`：釘選視窗生命週期
- `ScreenCanvasManager`：全螢幕標註 session
- `RecordingManager`：錄影 state machine
- `SingleInstanceGuard`：single-instance 保護
- `HotkeyManager`：全域熱鍵註冊
- `CLIHandler`：指令解析與 IPC 分派
- `UpdateChecker`：版本更新檢查

### Region selector subsystem

`src/region/` 內的重要抽取元件：

- `SelectionStateManager`
- `MagnifierPanel`
- `UpdateThrottler`
- `TextAnnotationEditor`
- `ShapeAnnotationEditor`
- `RegionExportManager`
- `RegionInputHandler`
- `RegionPainter`
- `MultiRegionManager`
- `RegionToolbarHandler`
- `RegionSettingsHelper`
- `SelectionDirtyRegionPlanner`
- `SelectionResizeHelper`
- `CaptureShortcutHintsOverlay`

### Pin window subsystem

`src/pinwindow/` 內的重要抽取元件：

- `ResizeHandler`
- `UIIndicators`
- `PinWindowPlacement`
- `RegionLayoutManager`
- `RegionLayoutRenderer`
- `PinMergeHelper`
- `PinHistoryStore`
- `PinHistoryWindow`

### Recording subsystem

- `RecordingRegionSelector`
- `RecordingControlBar`
- `RecordingBoundaryOverlay`
- `RecordingInitTask`
- `RecordingRegionNormalizer`

### CLI subsystem

`include/cli/commands/` 內的主要 commands：

- `FullCommand`
- `ScreenCommand`
- `RegionCommand`
- `GuiCommand`
- `CanvasCommand`
- `RecordCommand`
- `PinCommand`
- `ConfigCommand`

## 開發規範

### Logging

| 函式 | 用途 | Release 行為 |
|---|---|---|
| `qDebug()` | 開發診斷訊息 | 抑制 |
| `qWarning()` | 硬體或 API 失敗 | 會輸出 |
| `qCritical()` | 致命錯誤 | 會輸出 |

### Code style

- 適度使用 C++17 特性提升可讀性
- 複雜型別用 `auto`，簡單 primitive 保持顯式型別
- 使用 `nullptr`，不要用 `NULL`
- 優先使用 `QPoint` / `QRect`，避免鬆散座標組

### 測試

- 測試檔命名為 `tests/<Component>/tst_<Name>.cpp`
- 測試框架使用 Qt Test，例如 `QCOMPARE`、`QVERIFY`
- 目前測試資料夾包含 `Annotations`、`Beautify`、`CLI`、`Cursor`、`Detection`、`Encoding`、`Hotkey`、`IPC`、`MCP`、`PinWindow`、`Qml`、`RecordingManager`、`RegionSelector`、`ScreenCanvas`、`Settings`、`Share`、`Tools`、`Update`、`Utils`

### 避免的模式

- 已有 settings manager 卻直接存取 `QSettings`
- 大量重複 `switch`
- 沒有命名常數的 magic numbers

## 相關文件

- [從原始碼建置](/zh-tw/developer/build-from-source/)
- [發佈與打包](/zh-tw/developer/release-packaging/)
- [命令列](/zh-tw/docs/cli/)
