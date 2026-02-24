---
layout: docs
title: 疑難排解
description: 快速解決權限、啟動、編碼與快捷鍵衝突問題。
permalink: /zh-tw/docs/troubleshooting/
lang: zh-tw
route_key: docs_troubleshooting
doc_group: advanced
doc_order: 2
---

## 無法開始截圖

### macOS

- 確認已授權 Screen Recording
- 確認已授權 Accessibility
- 修改權限後重啟 SnapTray

### Windows

- 更新顯示卡驅動
- 確認必要 runtime 已安裝

## 快捷鍵無反應

- 到 Settings > Hotkeys 檢查是否重複綁定
- 改成其他組合後立即測試
- 確認是否被其他常駐工具攔截

## 錄影異常

- 出現掉幀時先降低 FPS
- 長時間錄影優先改用 MP4
- 重新確認音訊來源與裝置

## 建置或啟動錯誤

開發環境建議執行腳本確認一致性：

- macOS: `./scripts/build.sh`
- Windows: `scripts\\build.bat`

再跑測試腳本驗證整體環境。
