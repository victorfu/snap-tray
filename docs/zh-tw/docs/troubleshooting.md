---
last_modified_at: 2026-03-24
layout: docs
title: 疑難排解
description: 快速解決權限、啟動、Qt 部署、錄影與快捷鍵衝突問題。
permalink: /zh-tw/docs/troubleshooting/
lang: zh-tw
route_key: docs_troubleshooting
doc_group: advanced
doc_order: 2
---

## 無法開始截圖

### macOS

- 到 `System Settings > Privacy & Security > Screen Recording` 確認已授權
- 到 `System Settings > Privacy & Security > Accessibility` 確認已授權
- 修改任一權限後請重新啟動 SnapTray

### Windows

- 若擷取失敗或選取器沒有出現，先更新顯示卡驅動
- 若是本地開發 build，確認必要 runtime 已正確部署
- 只有在錄音時才需要確認麥克風權限

## 托盤圖示沒有出現

- 重新啟動 SnapTray
- 確認沒有被作業系統安全提示擋下
- 若你跑的是本地開發 build，重新執行平台 build script 並檢查依賴是否完整

若仍無法出現，請重新建置：

- macOS: `./scripts/build.sh`
- Windows: `scripts\build.bat`

## macOS 被 Gatekeeper 擋住

正式簽章並完成公證的版本理論上不會出現 Gatekeeper 警告。

若要手動驗證 DMG：

```bash
spctl -a -vv -t open "dist/SnapTray-<version>-macOS.dmg"
```

只有在本地 ad-hoc 或開發版本時，才建議清除 quarantine：

```bash
xattr -cr /Applications/SnapTray.app
```

## Windows 顯示缺少 DLL 或 Qt plugin

若出現 `Qt6Core.dll was not found` 或 `no Qt platform plugin could be initialized`，請用 `windeployqt` 部署 Qt 依賴：

```batch
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

Qt 路徑請與你 configure 時使用的 `CMAKE_PREFIX_PATH` 保持一致。

## 快捷鍵無反應

- 到 Settings > Hotkeys 確認動作仍有綁定
- 重新綁定後立即測試
- 檢查是否被其他全域熱鍵工具攔截
- 最常用動作建議保留為單一組合鍵

## 錄影異常

- 若掉幀，先降低 FPS
- 長時間錄影優先使用 MP4
- 重新確認音訊來源與裝置
- 在 macOS，系統音錄製需要 macOS 13+ 或虛擬音訊裝置，例如 BlackHole

## 建置或本地啟動錯誤

開發環境建議用 repo 腳本驗證整條工具鏈：

- macOS: `./scripts/build.sh` 後接 `./scripts/run-tests.sh`
- Windows: `scripts\build.bat` 後接 `scripts\run-tests.bat`

若你正在排查打包或簽章流程，請直接看[發佈與打包](/zh-tw/developer/release-packaging/)。
