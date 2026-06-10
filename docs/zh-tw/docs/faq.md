---
last_modified_at: 2026-03-26
layout: docs
title: 常見問題
seo_title: "SnapTray 常見問題：隱私、檔案格式、OCR、快捷鍵、自動化與平台限制"
description: 收錄隱私、格式、OCR、快捷鍵、自動化與平台限制等常見問答。
permalink: /zh-tw/docs/faq/
lang: zh-tw
route_key: docs_faq
doc_group: advanced
doc_order: 3
faq_schema:
  - question: "SnapTray 會上傳我的畫面嗎？"
    answer: "不會自動上傳。截圖、標註與釘選預設都在本機完成，只有你主動使用 Share URL 功能時才會上傳。錄影僅支援 macOS/Windows，且同樣在本機完成。"
  - question: "錄影格式要怎麼選？"
    answer: "在 macOS/Windows，MP4 適合長影片與教學，GIF 適合短循環或簡短動作演示，WebP 適合較輕量的動態片段。Linux beta 不提供錄影格式。"
  - question: "為什麼我的 OCR 無法使用？"
    answer: "OCR 僅支援 macOS/Windows，且取決於平台能力與系統已安裝的語言套件。Linux beta 會隱藏且不包含 OCR。"
  - question: "快捷鍵可以改嗎？"
    answer: "可以，在 Settings > Hotkeys 可依分類自訂。"
  - question: "可以用腳本控制 SnapTray 嗎？"
    answer: "可以。CLI 是官方自動化介面，支援本地擷取與 IPC 控制。"
  - question: "目前有哪些平台限制？"
    answer: "多螢幕與混合 DPI 擷取仍需要更多實機測試。在 macOS/Windows，當原生錄影 API 不可用時，可能回退到 Qt capture engine。macOS 系統音錄製需要 macOS 13+ 或虛擬音訊裝置如 BlackHole。Linux beta 是 Ubuntu 22.04 X11 AppImage；不包含錄影與 OCR，也不支援 Wayland。"
  - question: "哪裡回報問題最快？"
    answer: "到 GitHub issue 回報，並盡量附上 OS 版本、SnapTray 版本、可重現步驟，以及截圖或錄影。"
---

## SnapTray 會上傳我的畫面嗎？

不會自動上傳。截圖、標註與釘選預設都在本機完成，只有你主動使用 Share URL 功能時才會上傳。錄影僅支援 macOS/Windows，且同樣在本機完成。對於直接下載版本，SnapTray 會透過平台原生更新器檢查更新（macOS 使用 Sparkle，Windows 使用 WinSparkle），這可以在設定中關閉。

## 錄影格式要怎麼選？

錄影與動態格式匯出僅支援 macOS/Windows。在這些平台上：

- MP4：長影片、教學、示範
- GIF：短循環或簡短動作演示
- WebP：較輕量的動態片段

## 為什麼我的 OCR 無法使用？

OCR 僅支援 macOS/Windows，且取決於平台能力與系統已安裝的語言套件。Linux beta 會隱藏且不包含 OCR。

## 快捷鍵可以改嗎？

可以，在 Settings > Hotkeys 可依分類自訂。

## 可以用腳本控制 SnapTray 嗎？

可以。CLI 是官方自動化介面，支援本地擷取與 IPC 控制。完整指令請參閱[命令列](/zh-tw/docs/cli/)。

## 目前有哪些平台限制？

- 多螢幕與混合 DPI 擷取仍需要更多實機測試
- 在 macOS/Windows，當原生錄影 API 不可用時，SnapTray 可能回退到 Qt capture engine，速度會比較慢
- macOS 系統音錄製需要 macOS 13+ 或虛擬音訊裝置，例如 BlackHole
- Linux beta 是 Ubuntu 22.04 X11 AppImage；不包含錄影與 OCR，也不支援 Wayland session

## 哪裡回報問題最快？

到 GitHub issue 回報，並盡量附上：

- OS 版本
- SnapTray 版本
- 可重現步驟
- 必要時附截圖或錄影
