# Cursor Style 控制程式碼分析

## 概述

SnapTray 使用集中式的 `CursorManager` 來管理游標樣式，配合 F2 熱鍵觸發 Region Capture 時會切換到 CrossCursor。

---

## 1. F2 熱鍵觸發流程

### 熱鍵定義
**`include/settings/Settings.h:19-22`**
```cpp
inline constexpr const char* kDefaultHotkey = "F2";           // Region Capture
inline constexpr const char* kDefaultScreenCanvasHotkey = "Ctrl+F2";
inline constexpr const char* kDefaultQuickPinHotkey = "Shift+F2";
```

### 熱鍵註冊與觸發
1. **`src/hotkey/HotkeyManager.cpp:351-433`** - 註冊 QHotkey 實例
2. **`src/MainApplication.cpp:558-585`** - `handleHotkeyAction()` 分發熱鍵事件
3. **`src/MainApplication.cpp:347-364`** - `onRegionCapture()` 調用 `m_captureManager->startRegionCapture()`

---

## 2. CursorManager - 核心游標管理

**檔案位置：**
- `include/cursor/CursorManager.h`
- `src/cursor/CursorManager.cpp`

### 關鍵方法

| 方法 | 行號 | 說明 |
|------|------|------|
| `pushCursorForWidget()` | :31-66 | 依優先級推入游標 |
| `popCursorForWidget()` | :68-84 | 彈出游標 |
| `clearAllForWidget()` | :86-94 | 清除所有游標，預設回 CrossCursor |
| `applyCursorForWidget()` | :374-388 | **實際調用 `widget->setCursor()`** |
| `effectiveCursorForWidget()` | :390-403 | 取得最高優先級游標，預設 CrossCursor |

### 游標上下文優先級 (CursorContext)
```cpp
enum class CursorContext {
    Tool,       // 最低 - 工具游標
    Hover,      // 懸停目標
    Selection,  // 選取狀態
    Drag,       // 拖曳狀態
    Override    // 最高 - 覆蓋
};
```

---

## 3. RegionSelector 的 CrossCursor 設定

**`src/RegionSelector.cpp`**

### 初始化時設定 CrossCursor
**Line 119:**
```cpp
cm.pushCursorForWidget(this, CursorContext::Selection, Qt::CrossCursor);
```

### 選取狀態變更時
**Line 831:**
```cpp
CursorManager::instance().pushCursorForWidget(this, CursorContext::Selection, Qt::CrossCursor);
```

---

## 4. 其他設定 CrossCursor 的位置

| 檔案 | 行號 | 情境 |
|------|------|------|
| `src/RecordingRegionSelector.cpp` | :29 | 錄影區域選擇 |
| `src/RecordingAnnotationOverlay.cpp` | :113 | 錄影標註覆蓋層 |
| `src/cursor/CursorManager.cpp` | :292, 315, 348, 351 | 狀態驅動游標更新 |
| `src/tools/handlers/SelectionToolHandler.cpp` | :118-125 | Selection 工具 |
| `src/tools/handlers/LaserPointerToolHandler.cpp` | :21-22 | 雷射筆工具 |
| `src/tools/handlers/StepBadgeToolHandler.cpp` | :34-35 | 步驟標記工具 |
| `src/tools/handlers/EmojiStickerToolHandler.cpp` | :26-27 | 表情貼圖工具 |
| `include/tools/IToolHandler.h` | :133 | 工具基類預設 |

---

## 5. 工具處理器的游標覆寫

**`include/tools/IToolHandler.h:133`** - 基類預設：
```cpp
virtual QCursor cursor() const { return Qt::CrossCursor; }
```

### 特殊游標工具
- **MosaicToolHandler** (`src/tools/handlers/MosaicToolHandler.cpp:76-84`) - 自訂圓形游標
- **EraserToolHandler** (`src/tools/handlers/EraserToolHandler.cpp:94-100`) - 自訂橡皮擦游標

---

## 6. 完整觸發流程圖

```
F2 按下
  ↓
QHotkey::activated (HotkeyManager.cpp:422-424)
  ↓
HotkeyManager::onHotkeyActivated() → emit actionTriggered
  ↓
MainApplication::handleHotkeyAction() (MainApplication.cpp:564-565)
  ↓
MainApplication::onRegionCapture() (MainApplication.cpp:363)
  ↓
CaptureManager::startRegionCapture()
  ↓
RegionSelector 建構/顯示
  ↓
CursorManager::pushCursorForWidget(this, Selection, Qt::CrossCursor)
  ↓
CursorManager::applyCursorForWidget() → widget->setCursor()
```

---

## 7. 關鍵檔案清單

### 必讀檔案
- `include/cursor/CursorManager.h` - 游標管理器介面
- `src/cursor/CursorManager.cpp` - 游標管理器實作
- `src/RegionSelector.cpp` - Region Capture 主要邏輯
- `include/settings/Settings.h` - 熱鍵預設值
- `src/hotkey/HotkeyManager.cpp` - 熱鍵註冊
- `src/MainApplication.cpp` - 熱鍵事件處理

### 工具游標相關
- `include/tools/IToolHandler.h` - 工具基類
- `src/tools/handlers/*.cpp` - 各工具的 cursor() 實作
