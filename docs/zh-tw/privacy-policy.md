---
layout: page
title: 隱私政策
seo_title: "SnapTray 隱私政策：本機優先處理，不做遙測、不追蹤、不上傳內容"
permalink: /zh-tw/privacy/
description: SnapTray 隱私政策。本機優先，無遙測或追蹤。
lang: zh-tw
route_key: privacy
---

<p><strong>最後更新：</strong>2026 年 11 月</p>

## 概述

SnapTray 是一款以本機優先的截圖與標註應用程式，錄影功能僅支援 macOS 與 Windows。Linux beta 僅支援截圖與標註。僅在執行選擇性更新檢查時才會連線。我們重視隱私，並以透明方式說明資料處理方式。

**重點摘要：** SnapTray 以本機優先運作，不含遙測、追蹤或分析功能。網路存取僅限選擇性更新檢查（僅傳送版本號）。

本政策說明 SnapTray 桌面 App 的資料處理。snaptray.cc 網站另見[網站（snaptray.cc）](#website)。

## 資料蒐集

**我們不蒐集任何資料。** 包含但不限於：

- 不使用遙測或使用者行為分析
- 不上傳崩潰報告到外部伺服器
- 不進行追蹤或建立唯一識別碼

## 資料儲存

SnapTray 使用或產生的資料都儲存在你的本機：

| 資料類型 | 儲存位置 | 用途 |
|-----------|-----------|------|
| 截圖 | 使用者指定資料夾（預設：圖片） | 儲存截圖 |
| 錄影 | 使用者指定資料夾（預設：下載） | 儲存 macOS 與 Windows 錄影；Linux beta 不包含錄影功能 |
| 設定 | 作業系統設定儲存區* | 儲存偏好 |

*設定儲存位置：
- **macOS:** `~/Library/Preferences/cc.snaptray.plist`
- **Windows:** `HKEY_CURRENT_USER\Software\SnapTray`
- **Linux beta:** 使用 Qt/Linux 的使用者設定儲存區，位於使用者設定目錄下，常見形式為 `~/.config/<organization>/<application>.conf`
- 在 macOS 上，`cc.snaptray` 之前的舊版偏好設定檔會於清理時直接移除，不會匯入；Windows 舊版設定則會在清理前先行移轉。

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

## 網路通訊

SnapTray 僅在執行選擇性更新檢查時連線，不會上傳截圖。

### 更新檢查

SnapTray 會向 `api.github.com` 查詢是否有新版本。此功能預設啟用，可在設定中關閉。僅傳送應用程式版本號，不會傳送任何個人資料、使用資訊或裝置識別碼。透過商店安裝（如 Microsoft Store、Mac App Store）的版本會略過此檢查。

### SnapTray 不會做的事

- 不嵌入追蹤 SDK 或分析工具
- 不使用 cookies 或類似技術
- 不傳送遙測或崩潰報告

## 網站（snaptray.cc） {#website}

本節僅適用於 snaptray.cc 網站，不改變上述任何內容：SnapTray App 依然不蒐集任何資料。

網站會先詢問你是否同意使用統計。只有在你於網站統計提示中選擇**允許**後，網站才會載入 Google Analytics 4（由 Google 提供），用於統計造訪與下載意願。若你選擇**拒絕**、未回應提示，或瀏覽器送出 Global Privacy Control 或 Do Not Track 訊號，網站不會載入 Google Analytics。

你允許後，網站會傳送：

- 頁面瀏覽：不含查詢字串的頁面路徑，以及頁面語言
- 來源網站的網域（不含完整網址）
- 你點進來的連結所帶的活動標記（`utm_source`、`utm_medium`、`utm_campaign`、`utm_content`），僅限符合網站預先定義清單的值；其他值一律記為「other」
- 下載、Microsoft Store 與 GitHub 連結的點擊：平台、版本號，以及連結在頁面上的位置

為提供服務，Google 也會收到你的 IP 位址與瀏覽器資訊；Google Analytics 4 不會記錄或儲存 IP 位址。Google 信號與廣告個人化功能皆已關閉。網站不會傳送表單輸入、完整查詢字串、Email 或任何來自 SnapTray App 的內容。

Cookie 與瀏覽器儲存：

| 名稱 | 類型 | 設定時機 | 用途 | 保存期間 |
|------|------|----------|------|----------|
| `_ga`、`_ga_<ID>` | Cookie（Google Analytics） | 你允許統計後 | 區分重複造訪與工作階段 | 最長 2 年，或直到你拒絕 |
| `snaptray.analyticsConsent` | Local storage | 你選擇允許或拒絕時 | 記住你的選擇 | 12 個月 |
| `snaptray.attribution` | Session storage | 你允許統計後 | 瀏覽期間保留你的來源活動 | 分頁關閉，或 30 分鐘內未瀏覽新頁面時清除 |

Google Analytics 中的統計資料保存 2 個月後刪除；彙總報表（例如各活動的下載點擊總數）可能保存較久。

若要變更選擇，請使用任一頁面底部的**網站統計設定**。允許後再改為拒絕，會停止統計並刪除該瀏覽器中本網站的 Google Analytics Cookie。

統計數字僅為概估：廣告阻擋器、未同意的訪客與網路問題，都會使部分造訪未被計入。

## 開源與可驗證性

SnapTray 以 MIT 授權開源，你可以直接檢閱原始碼來驗證本政策：

- [GitHub Repository](https://github.com/victorfu/snap-tray)

## 政策更新

若本政策內容調整，會更新本頁「最後更新」日期。你可以定期查看本頁、檢閱版本資訊，或在設定中啟用更新通知以獲得最新資訊。

## 聯絡方式

若你對隱私政策有疑問，請至 [GitHub issues](https://github.com/victorfu/snap-tray/issues) 反映。

---

## 快速摘要

| 問題 | 答案 |
|----------|------|
| SnapTray 會蒐集資料嗎？ | 不會 |
| SnapTray 會上傳資料嗎？ | 僅在啟用更新檢查時（僅傳送版本號） |
| SnapTray 會追蹤使用行為嗎？ | 不會 |
| SnapTray 含分析 SDK 嗎？ | 不含 |
| SnapTray 會自動連線嗎？ | 不會。更新檢查預設開啟但可關閉，僅傳送版本號 |
| 檔案儲存在哪裡？ | 全部在你的本機 |
| 可以自行驗證嗎？ | 可以，原始碼公開 |
| snaptray.cc 網站會使用分析工具嗎？ | 僅在你於網站提示中允許後使用 Google Analytics 4，詳見[網站](#website) |
