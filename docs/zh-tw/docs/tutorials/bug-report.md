---
layout: docs
title: Bug 回報流程
description: 用清楚截圖、步驟與短影片，建立可重現的問題回報。
permalink: /zh-tw/docs/tutorials/bug-report/
lang: zh-tw
route_key: docs_tutorial_bug_report
doc_group: tutorials
doc_order: 3
---

## 目標

產出工程師第一次看就能重現的 bug 回報。

## 交付內容

- 1 張已標註的截圖
- 1 份精簡重現步驟
- 選配：10-30 秒錄影

## 操作流程

### 1. 重現一次並停在錯誤狀態

- 先完整重現問題。
- 停在錯誤最清楚的畫面。
- 若要比對預期與實際，可各截一張再釘選對照。

### 2. 擷取證據畫面

1. 按 `F2` 截圖。
2. 保留周邊脈絡（工具列、分頁名稱、狀態列）。
3. 不要裁太緊，避免失去重現線索。

### 3. 用標註輔助診斷

1. 用 `Step Badge` 標出流程順序（`1`、`2`、`3`）。
2. 用 `Arrow` 指向錯誤位置。
3. 用 `Text` 補上錯誤碼、異常值或時間。
4. 用 `Mosaic` 遮蔽 token 或個資。

### 4. 以結構化檔名輸出

建議檔名：

`bug-login-timeout-win10-2026-02-24.png`

### 5. 選配：補一段短錄影

若問題與時序、動畫或互動節奏有關：

1. 在選取區按 `R`（或從托盤啟動錄影）。
2. 錄 10-30 秒。
3. 停止並輸出 MP4。

## Bug 回報模板

```markdown
Title: [平台] 問題摘要

Environment:
- App version:
- OS:

Steps to reproduce:
1. ...
2. ...
3. ...

Expected:
Actual:

Attachments:
- screenshot.png
- recording.mp4 (optional)
```

## 品質檢查清單

- 重現步驟具穩定性。
- 截圖同時包含問題與必要脈絡。
- 標註有說明順序與影響範圍。
- 敏感資訊已處理。

## 下一篇

如果你要邊修邊比對，接著看 [Pin 長駐參考](/zh-tw/docs/tutorials/live-reference/)。
