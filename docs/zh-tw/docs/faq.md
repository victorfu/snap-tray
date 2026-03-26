---
layout: docs
title: 常見問題
description: 收錄隱私、格式、OCR、快捷鍵、自動化與平台限制等常見問答。
permalink: /zh-tw/docs/faq/
lang: zh-tw
route_key: docs_faq
doc_group: advanced
doc_order: 3
faq_schema:
  - question: "SnapTray 會上傳我的畫面嗎？"
    answer: "不會自動上傳。截圖、標註、釘選與錄影預設都在本機完成，只有你主動使用 Share URL 功能時才會上傳。"
  - question: "錄影格式要怎麼選？"
    answer: "MP4 適合長影片與教學，GIF 適合短循環或簡短動作演示，WebP 適合較輕量的動態片段。"
  - question: "為什麼我的 OCR 無法使用？"
    answer: "OCR 是否可用取決於平台能力與系統已安裝的語言套件。"
  - question: "快捷鍵可以改嗎？"
    answer: "可以，在 Settings > Hotkeys 可依分類自訂。"
  - question: "可以用腳本控制 SnapTray 嗎？"
    answer: "可以。CLI 是官方自動化介面，支援本地擷取與 IPC 控制。"
  - question: "目前有哪些平台限制？"
    answer: "多螢幕與混合 DPI 擷取仍需要更多實機測試。當原生錄影 API 不可用時，可能回退到 Qt capture engine。macOS 系統音錄製需要 macOS 13+ 或虛擬音訊裝置如 BlackHole。"
  - question: "哪裡回報問題最快？"
    answer: "到 GitHub issue 回報，並盡量附上 OS 版本、SnapTray 版本、可重現步驟，以及截圖或錄影。"
---

## SnapTray 會上傳我的畫面嗎？

不會自動上傳。截圖、標註、釘選與錄影預設都在本機完成，只有你主動使用 Share URL 功能時才會上傳。對於直接下載版本，SnapTray 會透過平台原生更新器檢查更新（macOS 使用 Sparkle，Windows 使用 WinSparkle），這可以在設定中關閉。

## 錄影格式要怎麼選？

- MP4：長影片、教學、示範
- GIF：短循環或簡短動作演示
- WebP：較輕量的動態片段

## 為什麼我的 OCR 無法使用？

OCR 是否可用取決於平台能力與系統已安裝的語言套件。

## 快捷鍵可以改嗎？

可以，在 Settings > Hotkeys 可依分類自訂。

## 可以用腳本控制 SnapTray 嗎？

可以。CLI 是官方自動化介面，支援本地擷取與 IPC 控制。完整指令請參閱[命令列](/zh-tw/docs/cli/)。

## 目前有哪些平台限制？

- 多螢幕與混合 DPI 擷取仍需要更多實機測試
- 當原生錄影 API 不可用時，SnapTray 可能回退到 Qt capture engine，速度會比較慢
- macOS 系統音錄製需要 macOS 13+ 或虛擬音訊裝置，例如 BlackHole

## 哪裡回報問題最快？

到 GitHub issue 回報，並盡量附上：

- OS 版本
- SnapTray 版本
- 可重現步驟
- 必要時附截圖或錄影
