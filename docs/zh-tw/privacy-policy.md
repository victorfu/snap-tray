---
layout: page
title: 隱私政策
permalink: /zh-tw/privacy/
description: SnapTray 隱私政策。所有截圖與錄影流程皆在本機完成。
lang: zh-tw
route_key: privacy
---

<p><strong>最後更新：</strong>2026 年 1 月</p>

## 概述

SnapTray 是一款完全在本機運作的截圖與錄影應用程式。我們重視隱私，並以透明方式說明資料處理方式。

**重點摘要：** SnapTray 不會蒐集、傳輸或分享你的資料。所有操作都留在你的裝置。

## 資料蒐集

**我們不蒐集任何資料。** 包含但不限於：

- 不使用遙測或使用者行為分析
- 不上傳崩潰報告到外部伺服器
- 不進行追蹤或建立唯一識別碼
- 不傳送螢幕內容、檔案或設定資料

## 資料儲存

SnapTray 使用或產生的資料都儲存在你的本機：

| 資料類型 | 儲存位置 | 用途 |
|-----------|-----------|------|
| 截圖 | 使用者指定資料夾（預設：圖片） | 儲存截圖 |
| 錄影 | 使用者指定資料夾（預設：下載） | 儲存錄影 |
| 設定 | 作業系統設定儲存區* | 儲存偏好 |

*設定儲存位置：
- **macOS:** `~/Library/Preferences/com.victorfu.snaptray.plist`
- **Windows:** `HKEY_CURRENT_USER\Software\Victor Fu\SnapTray`

## 權限需求

SnapTray 會依功能向系統請求必要權限：

### macOS

| 權限 | 用途 |
|------------|------|
| Screen Recording | 截圖與錄影核心功能 |
| Accessibility | 視窗偵測等進階擷取功能 |

### Windows

| 權限 | 用途 |
|------------|------|
| Full Trust | 支援 DXGI 擷取、WASAPI 音訊與 OCR 能力 |

**可選權限：**
- 麥克風僅在你啟用錄音時才會使用。

## 第三方服務

SnapTray 不整合第三方分析、雲端儲存或追蹤服務。應用程式：

- 不將使用內容傳送到外部伺服器
- 不嵌入追蹤 SDK
- 不使用 cookies

## 開源與可驗證性

SnapTray 以 MIT 授權開源，你可以直接檢閱原始碼來驗證本政策：

- [GitHub Repository](https://github.com/vfxfu/snap-tray)

## 政策更新

若本政策內容調整，會更新本頁「最後更新」日期。由於 SnapTray 不仰賴雲端同步政策通知，請定期查看本頁或版本資訊。

## 聯絡方式

若你對隱私政策有疑問，請至 [GitHub issues](https://github.com/vfxfu/snap-tray/issues) 反映。

---

## 快速摘要

| 問題 | 答案 |
|----------|------|
| SnapTray 會蒐集資料嗎？ | 不會 |
| SnapTray 會上傳資料嗎？ | 不會 |
| SnapTray 會追蹤使用行為嗎？ | 不會 |
| SnapTray 含分析 SDK 嗎？ | 不含 |
| 檔案儲存在哪裡？ | 全部在你的本機 |
| 可以自行驗證嗎？ | 可以，原始碼公開 |
