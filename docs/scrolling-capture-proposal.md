# Scrolling Capture (Long Screenshot) - 全自動方案

## 目標

實作一個**全自動**的滾動截圖功能：使用者只需選擇視窗、按下開始，其餘全部自動完成。

### 設計原則

1. **極簡操作** - 選視窗 → 開始 → 完成，三步驟
2. **全自動執行** - 自動滾動、自動擷取、自動拼接、自動偵測結束
3. **穩定優先** - 保守的預設參數，寧可慢但穩定

### 不包含範圍

- ❌ 任意區域滾動擷取（僅支援完整視窗）
- ❌ 手動/半自動模式作為主要流程
- ❌ 複雜的使用者控制介面

---

## 使用者體驗 (UX)

### 核心流程

```
┌─────────────────────────────────────────────────────────────────┐
│                                                                 │
│   1. 啟動          2. 選視窗           3. 自動執行              │
│   ─────────        ──────────          ────────────             │
│                                                                 │
│   快捷鍵或     →   滑鼠移動到目標  →   點擊 Start 後            │
│   選單觸發         視窗上方             全自動完成               │
│                    (即時高亮)           (可隨時停止)             │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 階段 1：視窗選擇模式

```
┌─────────────────────────────────────────────────────────┐
│  ┌─────────────────────────────────────────────────┐   │
│  │                                                 │   │
│  │          [目標視窗 - 高亮邊框]                   │   │
│  │                                                 │   │
│  └─────────────────────────────────────────────────┘   │
│                                                         │
│  ┌─────────────────────────────────────────────────┐   │
│  │  📷 滾動截圖 - 將滑鼠移到目標視窗              │   │
│  │                                                 │   │
│  │  目標: Chrome - Google Search                   │   │
│  │                                                 │   │
│  │        [ Start ]     [ Cancel ]                 │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

### 階段 2：自動擷取中

```
┌─────────────────────────────────────────────────────────┐
│  ┌─────────────────────────────────────────────────┐   │
│  │                                                 │   │
│  │          [目標視窗 - 邊框動畫]                   │   │
│  │                                                 │   │
│  └─────────────────────────────────────────────────┘   │
│                                                         │
│  ┌─────────────────────────────────────────────────┐   │
│  │  📷 擷取中...                                   │   │
│  │                                                 │   │
│  │  ████████████░░░░░░░░  已擷取 5 幀              │   │
│  │                                                 │   │
│  │              [ Stop ]                           │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

### 階段 3：完成預覽

```
┌─────────────────────────────────────────────────────────┐
│  ┌─────────────────────────────────────────────────┐   │
│  │                                                 │   │
│  │          [長截圖預覽 - 可捲動]                   │   │
│  │                                                 │   │
│  └─────────────────────────────────────────────────┘   │
│                                                         │
│  ┌─────────────────────────────────────────────────┐   │
│  │  ✅ 完成！共 12 幀，高度 8,450 px               │   │
│  │                                                 │   │
│  │  [ Save ]  [ Copy ]  [ Pin ]  [ Retry ]         │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

---

## 技術架構

### 參考實作：ShareX

本設計參考 [ShareX ScrollingCaptureManager](https://github.com/ShareX/ShareX/blob/master/ShareX.ScreenCaptureLib/ScrollingCaptureManager.cs) 的成熟實作。

### 元件圖

```
┌─────────────────────────────────────────────────────────┐
│                 ScrollingCaptureManager                 │
│                                                         │
│  State Machine:                                         │
│  [Idle] → [Selecting] → [Capturing] → [Preview/Done]   │
│              ↓              ↓                           │
│           Cancel          Stop                          │
└─────────────────────────────────────────────────────────┘
         │              │              │
         ▼              ▼              ▼
┌─────────────┐  ┌─────────────┐  ┌─────────────┐
│ Window      │  │ Auto        │  │ Image       │
│ Highlighter │  │ Scroller    │  │ Combiner    │
│             │  │             │  │             │
│ - 偵測游標  │  │ - 發送滾動  │  │ - 重疊比對  │
│   下方視窗  │  │ - 擷取幀    │  │ - 拼接圖片  │
│ - 繪製高亮  │  │ - 等待穩定  │  │ - 邊緣處理  │
└─────────────┘  └─────────────┘  └─────────────┘
```

---

## 核心演算法（參考 ShareX）

### 1. 主擷取迴圈

```cpp
// 擷取狀態（三級狀態，與 ShareX 一致）
enum class CaptureStatus {
    Successful,           // 完整成功，所有幀都有可靠匹配
    PartiallySuccessful,  // 部分成功，某些幀使用了 fallback
    Failed                // 失敗，無法找到有效匹配
};

void AutoScroller::captureLoop() {
    // 可選：自動滾動到頂部
    if (options.autoScrollTop) {
        scrollToTop(targetWindow);
        delay(options.startDelay);
    }

    CaptureStatus overallStatus = CaptureStatus::Successful;

    while (!stopRequested) {
        // 1. 擷取當前畫面
        QImage currentFrame = captureWindow(targetWindow);

        // 2. 與結果圖比對並拼接
        if (result.isNull()) {
            result = currentFrame.copy();  // 第一幀直接作為結果
        } else {
            CombineResult cr = combineImages(result, currentFrame);

            if (cr.status == CaptureStatus::Failed) {
                // 完全失敗，停止擷取
                overallStatus = CaptureStatus::Failed;
                break;
            } else if (cr.status == CaptureStatus::PartiallySuccessful) {
                // 使用了 fallback，標記整體為部分成功
                overallStatus = CaptureStatus::PartiallySuccessful;
            }
        }

        // 3. 檢查是否到底（先用 API，失敗則用圖片比對）
        if (isScrollReachedBottom(targetWindow, previousFrame, currentFrame)) {
            break;
        }

        // 4. 發送滾動事件
        sendScroll(targetWindow, options.scrollMethod, options.scrollAmount);

        // 5. 等待內容穩定
        delay(options.scrollDelay);

        previousFrame = currentFrame;
    }
}
```

### 2. 圖片重疊比對（核心算法）

**策略**：從結果圖底部向上掃描，尋找與當前幀**頂部**匹配的位置。

**關鍵理解**：
- 結果圖 (result) 是已累積的長截圖
- 當前幀 (current) 是新擷取的畫面
- 我們要找的是：result 的底部哪一行 = current 的第 0 行（頂部）
- 找到後，把 current 中「未重疊」的部分接到 result 後面

```
┌─────────────────────────┐
│      result (累積)       │
│                         │
│   ┌─────────────────┐   │  ← resultY：從這裡開始向上搜尋
│   │  重疊區域        │   │     尋找與 current 頂部匹配的行
│   │  (matchCount 行) │   │
└───┴─────────────────┴───┘
    ┌─────────────────┐
    │  重疊區域        │   ← current 頂部（第 0 行）
    │  (matchCount 行) │
    ├─────────────────┤
    │  新內容          │   ← 這部分要接到 result 後面
    │                 │
    └─────────────────┘
```

```cpp
struct CombineResult {
    CaptureStatus status;
    int matchIndex;       // result 中匹配起始位置
    int matchCount;       // 連續匹配行數
    int ignoreBottom;     // 底部忽略的行數（sticky footer）
};

CombineResult ImageCombiner::combineImages(QImage& result, const QImage& current) {
    const int width = current.width();
    const int resultHeight = result.height();
    const int currentHeight = current.height();

    // 忽略側邊區域（避免捲軸、浮動元素影響）
    // ShareX: Max(50, Width/20)，但不超過 Width/3
    int sideMargin = qBound(50, width / 20, width / 3);
    int compareWidth = width - (sideMargin * 2);

    // 搜尋範圍：兩者高度較小值的一半
    int matchLimit = qMin(resultHeight, currentHeight) / 2;

    // 最佳匹配記錄（需同時記錄三個值）
    int bestMatchCount = 0;
    int bestMatchIndex = -1;
    int bestIgnoreBottomOffset = 0;

    // 從結果圖底部向上搜尋
    for (int resultY = resultHeight - 1;
         resultY >= resultHeight - matchLimit; resultY--) {

        // 針對每個可能的匹配位置，偵測底部固定元素
        int ignoreBottomOffset = 0;
        if (options.autoIgnoreBottomEdge) {
            ignoreBottomOffset = detectBottomIgnoreOffset(
                result, resultY, current, sideMargin, compareWidth);
        }

        int matchCount = 0;

        // 逐行比對：result[resultY - y] vs current[y]
        // 從 result 的 resultY 行開始向上，與 current 的第 0 行開始向下比對
        for (int y = 0; y < matchLimit && (resultY - y) >= 0; y++) {
            if (compareLines(result, resultY - y,
                            current, y,
                            sideMargin, compareWidth)) {
                matchCount++;
            } else {
                break;  // 連續匹配中斷
            }
        }

        // 更新最佳匹配（同時記錄對應的 ignoreBottomOffset）
        if (matchCount > bestMatchCount) {
            bestMatchCount = matchCount;
            bestMatchIndex = resultY;
            bestIgnoreBottomOffset = ignoreBottomOffset;
        }
    }

    // 執行拼接
    if (bestMatchCount >= minMatchThreshold) {
        // 新內容起始位置 = 匹配行數（current 中第 matchCount 行開始是新內容）
        int newContentStart = bestMatchCount;
        appendToResult(result, current, newContentStart, bestIgnoreBottomOffset);

        // 記錄成功參數供 fallback 使用
        lastMatchCount = bestMatchCount;
        lastMatchIndex = bestMatchIndex;
        lastIgnoreBottomOffset = bestIgnoreBottomOffset;

        return {CaptureStatus::Successful, bestMatchIndex, bestMatchCount, bestIgnoreBottomOffset};
    }

    // Fallback：使用上次成功的參數
    if (lastMatchCount > 0) {
        appendToResult(result, current, lastMatchCount, lastIgnoreBottomOffset);
        return {CaptureStatus::PartiallySuccessful, lastMatchIndex, 0, lastIgnoreBottomOffset};
    }

    return {CaptureStatus::Failed, -1, 0, 0};
}
```

### 3. 行比對（像素級）

```cpp
bool ImageCombiner::compareLines(
    const QImage& img1, int y1,
    const QImage& img2, int y2,
    int xOffset, int width
) {
    // 直接比較記憶體（類似 ShareX 的 memcmp）
    const uchar* line1 = img1.constScanLine(y1) + (xOffset * 4);  // RGBA
    const uchar* line2 = img2.constScanLine(y2) + (xOffset * 4);

    return memcmp(line1, line2, width * 4) == 0;
}
```

### 4. 底部固定區域偵測（Sticky Footer）

**原理**：Sticky footer 是固定在視窗底部的元素，不隨滾動移動。
因此在不同幀中，這些區域的內容完全相同。我們從底部開始向上比對，
累計「相同」的行數，直到找到不同的行為止。

```cpp
int ImageCombiner::detectBottomIgnoreOffset(
    const QImage& result, int resultMatchY,
    const QImage& current,
    int sideMargin, int compareWidth
) {
    // 從底部開始向上比對 result 與 current
    // 累計「相同」的行數作為要忽略的高度

    int maxIgnore = current.height() / 3;  // 最多忽略 1/3 高度
    int ignoreOffset = 0;

    for (int offset = 0; offset < maxIgnore; offset++) {
        int resultY = result.height() - 1 - offset;
        int currentY = current.height() - 1 - offset;

        // 確保不會超出範圍
        if (resultY < 0 || currentY < 0) break;

        // 比較這一行是否相同
        if (compareLines(result, resultY, current, currentY,
                        sideMargin, compareWidth)) {
            // 相同 → 這行是 sticky footer 的一部分
            ignoreOffset++;
        } else {
            // 不同 → sticky footer 結束
            break;
        }
    }

    return ignoreOffset;
}
```

### 5. 圖片拼接實作

當找到匹配位置後，需要將 current 中的「新內容」接到 result 後面：

```cpp
void ImageCombiner::appendToResult(
    QImage& result,
    const QImage& current,
    int newContentStart,      // current 中新內容的起始行
    int ignoreBottomOffset    // 要忽略的底部行數
) {
    // 計算要附加的高度
    int appendHeight = current.height() - newContentStart - ignoreBottomOffset;

    if (appendHeight <= 0) return;  // 沒有新內容

    // 建立新的結果圖
    int newHeight = result.height() + appendHeight;
    QImage newResult(result.width(), newHeight, result.format());

    // 複製原有結果
    QPainter painter(&newResult);
    painter.drawImage(0, 0, result);

    // 附加新內容（從 current 的 newContentStart 行開始）
    QRect sourceRect(0, newContentStart, current.width(), appendHeight);
    painter.drawImage(0, result.height(), current, sourceRect);

    result = std::move(newResult);
}
```

**計算說明**：
- `matchCount` = 重疊區域的行數
- `newContentStart` = matchCount（current 中第 matchCount 行開始是新內容）
- `appendHeight` = current.height() - newContentStart - ignoreBottomOffset

```
假設：
- current 高度 = 800px
- matchCount = 300 (與 result 重疊 300 行)
- ignoreBottomOffset = 50 (底部 50px 是 sticky footer)

則：
- newContentStart = 300
- appendHeight = 800 - 300 - 50 = 450px
- 將 current 的第 300~749 行接到 result 後面
```

### 6. 滾動到底偵測

ShareX 使用兩種方法互補：先嘗試系統 API，若 API 不可用則 fallback 到圖片比對。

```cpp
bool AutoScroller::isScrollReachedBottom(
    WId window,
    const QImage& prevFrame,
    const QImage& currFrame
) {
    // 方法 1 (Windows): 使用 GetScrollInfo API
    #ifdef Q_OS_WIN
    SCROLLINFO si = {sizeof(SCROLLINFO)};
    si.fMask = SIF_RANGE | SIF_PAGE | SIF_TRACKPOS;

    if (GetScrollInfo((HWND)window, SB_VERT, &si)) {
        // ShareX: maxPosition == currentTrackPosition + pageSize - 1
        int maxPosition = si.nMax - qMax((int)si.nPage - 1, 0);
        if (si.nTrackPos >= maxPosition) {
            return true;
        }
        // API 成功但尚未到底，不需要 fallback
        return false;
    }
    // API 失敗，繼續使用圖片比對
    #endif

    // 方法 2 (通用 / Fallback): 比較連續兩幀
    // ShareX 使用 CompareImages 做完整 memcmp 比對
    // 如果兩幀完全相同，表示滾動沒有效果 → 已到底
    return compareImages(prevFrame, currFrame);
}

// 完整圖片比對（用於滾動到底偵測）
bool AutoScroller::compareImages(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) return false;
    if (a.format() != b.format()) return false;

    // 逐行 memcmp 比對
    for (int y = 0; y < a.height(); y++) {
        const uchar* lineA = a.constScanLine(y);
        const uchar* lineB = b.constScanLine(y);
        if (memcmp(lineA, lineB, a.bytesPerLine()) != 0) {
            return false;
        }
    }
    return true;
}

// 可選：取樣相似度計算（用於部分匹配場景）
double AutoScroller::calculateSimilarity(const QImage& a, const QImage& b) {
    if (a.size() != b.size()) return 0.0;

    int matchingPixels = 0;

    // 取樣比較（每隔 N 個像素比較一次，提升效能）
    const int sampleStep = 4;
    int sampledPixels = 0;

    for (int y = 0; y < a.height(); y += sampleStep) {
        const QRgb* lineA = (const QRgb*)a.constScanLine(y);
        const QRgb* lineB = (const QRgb*)b.constScanLine(y);
        for (int x = 0; x < a.width(); x += sampleStep) {
            if (lineA[x] == lineB[x]) matchingPixels++;
            sampledPixels++;
        }
    }

    return (double)matchingPixels / sampledPixels;
}
```

---

## 平台實作

### 滾動方式（多種備選）

參考 ShareX，提供多種滾動方式以應對不同應用程式：

```cpp
enum class ScrollMethod {
    MouseWheel,     // 滑鼠滾輪（最通用）- ShareX 預設
    SendMessage,    // WM_VSCROLL 訊息
    KeyDown,        // 方向鍵 ↓
    PageDown        // Page Down 鍵
};

// Windows 實作
void sendScroll(WId window, ScrollMethod method, int amount) {
    HWND hwnd = (HWND)window;

    switch (method) {
    case ScrollMethod::MouseWheel: {
        // 確保視窗在前景
        SetForegroundWindow(hwnd);
        // ShareX: -120 * amount（WHEEL_DELTA = 120）
        INPUT input = {};
        input.type = INPUT_MOUSE;
        input.mi.dwFlags = MOUSEEVENTF_WHEEL;
        input.mi.mouseData = -WHEEL_DELTA * amount;  // 負值 = 向下
        SendInput(1, &input, sizeof(INPUT));
        break;
    }
    case ScrollMethod::SendMessage:
        // 直接發送捲軸訊息
        for (int i = 0; i < amount; i++) {
            SendMessage(hwnd, WM_VSCROLL, SB_LINEDOWN, 0);
        }
        break;
    case ScrollMethod::KeyDown:
        SetForegroundWindow(hwnd);
        for (int i = 0; i < amount; i++) {
            keybd_event(VK_DOWN, 0, 0, 0);
            keybd_event(VK_DOWN, 0, KEYEVENTF_KEYUP, 0);
        }
        break;
    case ScrollMethod::PageDown:
        SetForegroundWindow(hwnd);
        keybd_event(VK_NEXT, 0, 0, 0);
        keybd_event(VK_NEXT, 0, KEYEVENTF_KEYUP, 0);
        break;
    }
}

// Windows: 滾動到頂部
void scrollToTop(WId window) {
    HWND hwnd = (HWND)window;
    SetForegroundWindow(hwnd);

    // 方法 1: 發送 HOME 鍵
    keybd_event(VK_HOME, 0, 0, 0);
    keybd_event(VK_HOME, 0, KEYEVENTF_KEYUP, 0);

    // 方法 2: 發送 SB_TOP 訊息
    SendMessage(hwnd, WM_VSCROLL, SB_TOP, 0);
}

// macOS 實作
void sendScroll(WId window, ScrollMethod method, int amount) {
    // macOS 主要使用 CGEvent 發送滾輪事件
    CGEventRef event = CGEventCreateScrollWheelEvent(
        NULL,
        kCGScrollEventUnitLine,
        1,
        -amount  // 負值 = 向下
    );
    CGEventPost(kCGHIDEventTap, event);
    CFRelease(event);
}
```

### 視窗擷取

複用現有架構：
- Windows: `DXGICaptureEngine` 擷取指定視窗區域
- macOS: `SCKCaptureEngine` 使用 ScreenCaptureKit

---

## 設定參數

### 預設值（參考 ShareX）

```cpp
struct ScrollingCaptureOptions {
    // 時間控制
    int startDelayMs = 300;       // 開始前延遲
    int scrollDelayMs = 300;      // 每次滾動後延遲

    // 滾動控制
    ScrollMethod scrollMethod = ScrollMethod::MouseWheel;
    int scrollAmount = 2;         // 滾動量（行數或輪次）
    bool autoScrollTop = false;   // 是否先滾到頂部

    // 比對控制
    bool autoIgnoreBottomEdge = true;   // 自動偵測 sticky footer
    int minMatchLines = 10;             // 最少匹配行數

    // 安全限制
    int maxFrames = 50;           // 最大幀數
    int maxHeightPx = 20000;      // 最大圖片高度
};
```

### 進階設定 UI

大部分使用者使用預設值即可，但提供進階設定給特殊情況：

| 設定 | 說明 | 預設值 |
|------|------|--------|
| 滾動方式 | MouseWheel / PageDown / Arrow | MouseWheel |
| 滾動延遲 | 每次滾動後等待時間 | 300ms |
| 滾動量 | 每次滾動的幅度 | 2 |
| 自動偵測底部 | 偵測 sticky footer | ✓ |
| 先滾到頂部 | 開始前先 scroll to top | ✗ |

---

## 實作階段

### Phase 1：UI 框架與視窗選擇（1 週）

- [ ] `ScrollingCaptureManager` 狀態機
- [ ] 視窗選擇 UI（高亮 overlay）
- [ ] 控制面板 UI（Start/Stop/Cancel）
- [ ] 整合到 MainApplication 與系統匣選單

**驗收**：可選擇視窗，UI 流程完整

### Phase 2：自動滾動與擷取（1.5 週）

- [ ] Windows 滾動事件發送（4 種方式）
- [ ] macOS 滾動事件發送
- [ ] 自動擷取迴圈
- [ ] 滾動到底偵測（GetScrollInfo + 圖片比對）

**驗收**：可自動滾動並擷取多幀，自動停止於底部

### Phase 3：圖片拼接（1.5 週）

- [ ] `ImageCombiner` 重疊比對算法
- [ ] 側邊忽略邏輯
- [ ] 底部固定區域偵測 (AutoIgnoreBottomEdge)
- [ ] Fallback 匹配邏輯
- [ ] 記憶體管理（大圖處理）

**驗收**：自動擷取的幀可正確拼接成長圖

### Phase 4：穩定性調優（1 週）

- [ ] 各類視窗測試（瀏覽器、PDF、聊天軟體）
- [ ] 不同滾動方式的 fallback 機制
- [ ] 錯誤處理與使用者回饋
- [ ] 效能優化

**驗收**：主流應用程式 > 90% 成功率

---

## 檔案結構

```
include/scrolling/
├── ScrollingCaptureManager.h    # 主控制器
├── AutoScroller.h               # 自動滾動擷取
├── ImageCombiner.h              # 圖片拼接（重疊比對）
├── ScrollSender.h               # 滾動事件發送介面
├── WindowHighlighter.h          # 視窗高亮 UI
└── ScrollingCaptureOptions.h    # 設定

src/scrolling/
├── ScrollingCaptureManager.cpp
├── AutoScroller.cpp
├── ImageCombiner.cpp
├── WindowHighlighter.cpp
├── ScrollSender_win.cpp         # Windows 滾動（4 種方式）
├── ScrollSender_mac.mm          # macOS 滾動
└── ScrollingCapturePanel.cpp    # 控制面板 UI

tests/ScrollingCapture/
├── tst_ImageCombiner.cpp        # 重疊比對測試
├── tst_BottomEdgeDetection.cpp  # Sticky footer 偵測
├── tst_EndOfScrollDetection.cpp # 滾動到底偵測
└── tst_SimilarityCalculation.cpp
```

---

## 品質目標

| 指標 | 目標 |
|------|------|
| 靜態頁面成功率 | > 95% |
| 一般網頁成功率 | > 85% |
| 使用者操作步驟 | ≤ 3 步 |
| 單次擷取總時間 | < 30 秒（一般頁面）|
| 最大支援高度 | 20,000 px |

---

## 與 ShareX 的差異

| 項目 | ShareX | 本方案 |
|------|--------|--------|
| 平台 | Windows only | Windows + macOS |
| 圖片比對 | memcmp 像素比對 | 同（非 OpenCV） |
| UI | 複雜選項 | 極簡（只有 Start/Stop） |
| 滾動方式 | 4 種 + 手動選擇 | 4 種 + 自動 fallback |
| 狀態回報 | 三級狀態 | 同（Successful/PartiallySuccessful/Failed）|
| 側邊忽略 | Max(50, W/20)，上限 W/3 | 同 |
| 匹配閾值 | 高度的 50% | 同 |

### ShareX 核心設計要點（本方案遵循）

1. **比對方向**：result 底部 vs current 頂部（非 current 底部）
2. **最佳記錄**：同時記錄 matchCount、matchIndex、ignoreBottomOffset 三個值
3. **Fallback 機制**：使用上次成功的完整參數組，而非僅 offset
4. **滾動到底**：優先使用系統 API，失敗才用圖片比對
5. **Sticky footer**：從底部向上累計「相同」行數

### 選擇 memcmp 而非 OpenCV 的原因

1. **精確度**：像素級比對更準確，不會有模糊匹配的問題
2. **效能**：memcmp 比 OpenCV matchTemplate 快很多
3. **簡單**：不需要額外的 OpenCV 依賴（雖然專案已有）
4. **ShareX 驗證**：經過多年實戰驗證的算法

---

## 已知限制

1. **動態內容** - 有動畫/影片的區域可能拼接不完美
2. **彈出視窗** - 擷取中出現對話框會干擾
3. **非標準滾動** - 某些應用程式不響應標準滾動事件
4. **受保護內容** - DRM 保護的視窗可能無法擷取

---

## 與現有架構整合

1. **Library 歸屬**
   - `ImageCombiner` → `snaptray_algorithms`
   - 其他元件 → `snaptray_ui`

2. **入口點**
   - `MainApplication::startScrollingCapture()`
   - 系統匣選單新增「滾動截圖 / Scrolling Capture」
   - 可綁定全域快捷鍵

3. **輸出整合**
   - Save：使用現有 `FileSettingsManager` 路徑
   - Copy：複製到剪貼簿
   - Pin：開啟 `PinWindow` 顯示結果
