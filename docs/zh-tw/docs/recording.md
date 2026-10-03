---
last_modified_at: 2026-03-26
layout: docs
title: 錄影
seo_title: "SnapTray 螢幕錄影：macOS/Windows 輸出 MP4、GIF 與 WebP"
description: "僅 macOS/Windows：使用 MP4、GIF、WebP 輸出全螢幕來源錄影。"
permalink: /zh-tw/docs/recording/
lang: zh-tw
route_key: docs_recording
doc_group: workflow
doc_order: 2
---

錄影僅 macOS 與 Windows 提供。Linux beta 不包含錄影，錄影 UI 也會隱藏。

## 錄影入口

- 托盤選單：Record Screen
- 錄影快捷鍵：至 Settings > Hotkeys 設定 Record Screen

## 錄影生命週期

1. 多螢幕環境下，依提示選擇要錄製的螢幕。
2. 錄影會在選定螢幕上直接開始。
3. 透過浮動控制列查看時間並停止錄影。
4. 按 Stop 匯出檔案。

## 裁切錄影

錄影一律擷取整個螢幕。若只想保留其中一部分，請在停止錄影後於預覽視窗中裁切：

1. 在預覽工具列中按一下 **裁切**。
2. 在影片上拖曳以畫出裁切範圍，再於範圍內拖曳可移動，或拖曳控點調整大小。邊緣會吸附到影片邊緣與中心線。將游標移到視窗上會亮起，點一下即可裁切成該視窗在影片目前時間點的範圍。
3. 按 **Enter** 套用，或按 **Esc** 取消。套用後的尺寸會顯示在影片左上角，按 ✕ 即可清除。
4. 另存為 MP4、GIF 或 WebP。匯出的 MP4 會保留音訊。

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

畫質滑桿決定的是儲存後影片的畫質。開啟預覽時,錄影本身會以高畫質擷取,讓裁剪與修剪不會損失細節;儲存時再轉成所選畫質,若檔案本來就夠小則直接保留原檔。若暫存錄影所在的磁碟剩餘空間不足約十分鐘的高畫質錄影,SnapTray 會改以所選畫質錄製並顯示警告。關閉預覽時,錄影會直接以所選畫質寫入。
