---
last_modified_at: 2026-10-20
layout: docs
title: 區域截圖
seo_title: "SnapTray 區域截圖：放大鏡、視窗偵測、多區域與快速匯出的高精度擷取"
description: 透過放大鏡、視窗偵測、多區域模式與快速匯出動作完成高精度擷取。
permalink: /zh-tw/docs/region-capture/
lang: zh-tw
route_key: docs_region_capture
doc_group: capture
doc_order: 1
---

## 啟動截圖

按預設熱鍵 `F2` 進入 Region Capture，游標會切換為十字準星；游標旁輔助視覺會依設定中的 `Cursor companion` 模式顯示。

## 全螢幕截圖

從 tray 選單使用 `Full Screen Capture`，或在 Settings > Hotkeys 綁定快捷鍵，不需選取區域即可擷取游標所在的螢幕。每個螢幕都以原生解析度輸出，包含 125%、150% 等非整數縮放。SnapTray 自己的視窗（例如 Pin 與通知）不會出現在截圖中。

在 Settings > Advanced > `After full screen capture` 選擇擷取後的動作：

- `Copy to clipboard`（預設）
- `Save to file`，依照 Files 設定儲存
- `Open editor`，以選取整個螢幕的狀態開啟編輯，可先標註再複製或儲存

截圖會加入 History。

## 延遲截圖

從 tray 選單的 `Delayed Capture` 子選單選擇 `3 秒`、`5 秒` 或 `10 秒`，可擷取一按快捷鍵就會消失的 UI，例如 hover 狀態、下拉選單、右鍵選單與 tooltip。倒數期間畫面上方會顯示小型倒數提示；它不會攔截滑鼠與鍵盤，所以你可以照常打開選單或將游標移到想擷取的控制項上。

倒數結束後，SnapTray 會先隱藏倒數提示、凍結游標所在螢幕的畫面，再以該畫面進入 Region Capture。之後的流程與一般區域截圖相同，倒數提示不會出現在截圖中。

- tray 會記住上次選擇的秒數。在 Settings > Hotkeys 綁定 `Delayed Capture` 快捷鍵，即可用鍵盤以該秒數啟動倒數。
- 若要取消，從 tray 選單選擇 `Cancel Delayed Capture`，或再按一次 `Delayed Capture` 快捷鍵。倒數期間再次觸發截圖會被忽略。

## 選取行為

### 精準定位

- 放大鏡模式會提供像素級預覽
- 選取框旁會顯示寬高尺寸
- 按住 `Shift` 可鎖定比例

### 視窗感知選取

當平台支援視窗偵測時，可自動辨識游標下視窗並一鍵選取。

### 取消重選

右鍵或 `Esc` 可取消當前選取。

## 多區域流程

按 `M` 切換多區域擷取。你可累積多個區塊，再選擇合併輸出或分開儲存。

## 選取完成後可用動作

完成選取後，工具列可直接執行：

- 標註與編修工具（Arrow/Pencil/Marker/Shape/Text/Mosaic/Step Badge/Emoji/Eraser）
- `OCR`（僅 macOS/Windows；Linux beta 會隱藏）與 `QR Code Scan`
- `Auto Blur`（臉孔 + 憑證等敏感資訊）
- `Pin`、`Save`、`Copy`

## 常用快捷鍵

| 動作 | 快捷鍵 |
|---|---|
| 啟動區域截圖 | `F2` |
| 複製游標下色碼（放大鏡模式啟用時） | `C` |
| 鎖定比例 | 按住 `Shift` |
| 切換多區域 | `M` |
| 取消 | `Esc` |

## 下一步

前往[標註工具](/zh-tw/docs/annotation-tools/)學習輸出前編修流程。
若要在 macOS 或 Windows 進行全螢幕錄影，請使用[錄影](/zh-tw/docs/recording/)。
