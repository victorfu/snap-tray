---
last_modified_at: 2026-03-26
layout: docs
title: 錄影
description: 使用 MP4、GIF、WebP 輸出全螢幕來源錄影。
permalink: /zh-tw/docs/recording/
lang: zh-tw
route_key: docs_recording
doc_group: workflow
doc_order: 2
---

## 錄影入口

- 托盤選單：Record Screen
- CLI：`snaptray record start [--screen N]`

## 錄影生命週期

1. 多螢幕環境下，依提示選擇要錄製的螢幕。
2. 錄影會在選定螢幕上直接開始。
3. 透過浮動控制列查看時間並停止錄影。
4. 按 Stop 匯出檔案。

## 輸出格式

| 格式 | 適用情境 |
|---|---|
| MP4 (H.264) | 長時教學、產品 Demo |
| GIF | 短循環示意 |
| WebP | 輕量動態片段 |

## 音訊選項

目前僅 MP4 錄影會在平台與音源支援時提供音訊：

- 麥克風
- 系統音訊
- 混合錄音

GIF 與 WebP 輸出不包含音訊。

## 品質調校

在 Settings > Recording 可調整 FPS、畫質、倒數計時與預覽行為。
