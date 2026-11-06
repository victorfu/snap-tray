---
last_modified_at: 2026-11-06
layout: docs
title: 設定
seo_title: "SnapTray 設定說明：擷取、Pin 視窗、儲存、平台限定錄影與文字辨識"
description: 調整擷取、Pin 視窗、儲存與浮水印、平台限定錄影與文字辨識、快捷鍵及更新策略。
permalink: /zh-tw/docs/settings/
lang: zh-tw
route_key: docs_settings
doc_group: workflow
doc_order: 4
---

## 設定頁面

頁面依任務分組，除 About 外，每頁開頭有標題和一句說明。Updates 與 About 在分隔線下方。

Recording 與 Text Recognition 僅 macOS 與 Windows 提供。Linux beta 不包含這些功能，
因此會隱藏這兩頁。

### General

開機啟動、顯示語言、App 主題與 CLI 安裝/移除。

macOS 另外提供 Screen Recording / Accessibility 權限狀態與一鍵開啟系統設定捷徑。

### Capture

- **After Capture**：`After full screen capture` 要做什麼（複製、存檔或開啟編輯器）。
- **Region Selector**：游標旁輔助視覺（放大鏡）與快捷鍵提示。
- **Auto Blur**：Auto Blur 遮住臉部與文字時使用的模糊類型與強度。

### Pin Windows

預設不透明度、**Show controls on hover**，以及每次調整不透明度或縮放的步進。

### Recording

幀率、預設輸出格式、預覽優先的匯出流程、音訊來源/裝置與倒數計時。

### Saving

- 截圖/錄影輸出資料夾與檔名格式模板。
- 自動儲存與「記住上次使用的資料夾」：開啟後，Region、Pin、Beautify，以及素材庫的「另存新檔」對話框會從上次成功手動儲存圖片的資料夾開啟；自動儲存螢幕截圖仍使用「螢幕截圖」資料夾。
- **Watermark**：浮水印圖片、是否套用到截圖/錄影、不透明度、縮放、邊距與位置預覽。
- **Library**：素材庫保留多少張截圖。超過時，最舊的會從素材庫移除；你儲存的檔案不受影響。

### Text Recognition

OCR 語言優先序與辨識後行為。

### Hotkeys

依功能分類集中管理快捷鍵。每個快捷鍵可以個別重設，**Restore All Defaults** 會全部重設。

### Updates

目前版本、自動更新檢查頻率、上次檢查狀態與立即檢查。

### About

App 名稱、版本、版權、作者與網站連結。

## 搜尋與重設設定

- 側欄上方的 **搜尋設定**（`Ctrl+F`，macOS 為 `Cmd+F`）可依名稱、區塊（例如「浮水印」）、頁面找設定，也能依動作名稱找快捷鍵。用 `Up`／`Down` 與 `Enter` 開啟結果：SnapTray 會切到該頁、捲到該設定並短暫標示。`Esc` 清除搜尋。
- 設定變更後立即儲存。設定和預設值不同時，名稱左邊會出現 **重設** 按鈕，只還原這一項。快捷鍵維持原本的 **Reset** 與 **Restore All Defaults**。取決於系統或只能由你決定的設定，例如語言、主題、開機啟動、輸入裝置、浮水印圖片與 OCR 語言，沒有重設按鈕。

## 推薦基準配置

- macOS/Windows 的一般教學使用 30 FPS MP4
- 截圖與可用時的錄影分開資料夾管理
- 每天自動檢查更新

## 下一步

若發生權限或輸出異常，請查看[疑難排解](/zh-tw/docs/troubleshooting/)。
