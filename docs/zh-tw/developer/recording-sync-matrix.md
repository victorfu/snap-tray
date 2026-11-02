---
last_modified_at: 2026-11-02
layout: docs
title: 錄影同步驗證矩陣
seo_title: "SnapTray 錄影同步驗證矩陣：端到端影音同步與故障注入測試"
description: SnapTray 如何以合成媒體、故障注入、解碼後檢查、A/B 報告與實機流程，端到端驗證錄影。
permalink: /zh-tw/developer/recording-sync-matrix/
lang: zh-tw
route_key: developer_recording_sync_matrix
nav_data: developer_nav
docs_copy_key: developer
---

# 錄影同步驗證矩陣

`Encoding_SyncMatrix` 端到端驗證錄影：合成的影格與音訊經過錄影實際使用的時間元件（`SessionClock`、`FramePacer`、`TimestampedPcmMixer`）進入平台的編碼器與封裝，過程中注入故障；寫出的檔案再被解碼並逐格檢查。只有解碼後的媒體正確才算通過；狀態機跑完或播放器能開檔都不算證據。

原始碼：`tests/Encoding/SyncMatrix.{h,cpp}`（測試工具與分析）、`tests/Encoding/tst_SyncMatrix.cpp`（各情境）。

## 產生的內容

- **影像**：每一格以 16 格條碼寫入影格編號，加上移動方塊，並在固定時間（從 350 ms 起每 700 ms）整面白色閃爍。靜止區段會重複同一編號，模擬畫面沒有變化。
- **音訊**：每次閃爍時發出 40 ms、1 kHz 的脈衝，來源可以是麥克風、系統音訊或兩者混合，以 10 ms 的擷取回呼送出，時間戳記方式與實際擷取引擎相同。

## 故障

來源時間戳記與回呼到達時間分開控制：時間戳記抖動、延遲與突發到達、掉格／重複／倒退的影格、靜止畫面、提早與重複的暫停／繼續、單一音源暫時無聲、音訊時鐘偏移與漂移，以及編碼器暫時無法使用（在模擬時鐘上每 2 秒停頓一次；期間影格進入模擬編碼工作佇列，超過 30 格／3 秒的上限時會像實際工作佇列一樣被拒收）。

## 檢查項目（解碼後的媒體）

- 每個預期的影格都存在（逐次計數，靜止畫面遺失的重複影格也會被算到）、順序正確，並在預期時間呈現（誤差在半格內）；沒有暫停期間的影格，暫停處也沒有空隙。
- 結尾完整：最後一格的編號與結束時間。
- 唯一影格與重複影格分開計算（`uniqueFps`），不用重複影格湊出影格率。
- 要求音訊時音軌存在、長度涵蓋影片，且每次閃爍都在一個影格間隔加 25 ms 內有對應脈衝（`worstAvOffsetMs`）。

預期值來自同一批事件、同一套時間元件、但不注入錯誤的參考執行。

## 證明檢查有效

`detect-*` 情境會刻意注入錯誤或管線不會修正的故障，分析必須回報失敗：未移除的暫停、音訊位移 120 ms、結尾被截掉 500 ms、倒退的時間戳記送進編碼器（編碼器會夾住，使影格出現在錯的時間），音訊引擎時鐘晚 150 ms 開始，以及靜止畫面的重複影格遺失。

## 執行

```bash
./scripts/run-tests.sh                         # 包含快速情境（約 10 秒）
./scripts/run-sync-matrix.sh                   # 快速情境，JSON 報告存於 build/sync-matrix/<commit>
./scripts/run-sync-matrix.sh reports/a --full  # 加上 4K30、4K60 與長片 1080p30
SNAPTRAY_SYNC_LONG_S=600 ./scripts/run-sync-matrix.sh reports/b --full   # 10 分鐘長片
scripts/sync-matrix-compare.py reports/a reports/b   # 跨 commit A/B 比較
```

每份報告保存情境設定、預期值與量測值、失敗與略過的檢查，以及成本：實際與 CPU 時間、常駐記憶體、檔案大小、寫入影格數與編碼器拒收數。比較腳本在原本通過的情境失敗、或影音偏移增加超過 10 ms 時以狀態 1 結束。`detect-*` 情境以「有回報注入的錯誤」為通過（每份報告都記錄 `mustPass` 與 `testSucceeded`）；候選結果缺少某個情境、候選目錄為空或不存在時，都視為錯誤。成本變化只列出、不判定，直到每台機器建立基準。只比較同一台機器、建置類型、編碼器與電源模式的結果。

## 平台

| 平台 | 結果 |
| --- | --- |
| Linux（FFmpeg／VA-API） | 完整：編碼、解碼與分析 |
| macOS、Windows | **略過，不算通過**：解碼後分析使用 Linux 建置的 FFmpeg 讀取器，測試程式會明確回報略過。 |

原生 buffer、HDR 轉 SDR、混合 DPI、控制視窗排除與實際裝置變更無法以模擬取代，需要下方的實機流程。

## 實機流程（macOS、Windows、Linux 硬體）

1. 製作含閃爍與嗶聲的參考影片：
   ```bash
   ffmpeg -f lavfi -i "color=c=gray:s=1920x1080:r=60,drawbox=enable='lt(mod(t,1),0.05)':c=white:t=fill" \
          -f lavfi -i "sine=f=1000:sample_rate=48000,volume=enable='gte(mod(t,1),0.05)':volume=0" \
          -t 60 -shortest -pix_fmt yuv420p -c:v libx264 -c:a aac reference.mp4
   ```
2. 在要錄製的螢幕上全螢幕播放並開啟系統音訊，用 SnapTray 錄製（另外錄一次中途暫停並繼續，以及一次讓麥克風靠近喇叭）。
3. 量測錄影中的閃爍與嗶聲：
   ```bash
   ffmpeg -i recording.mp4 -vf "signalstats,metadata=print:key=lavfi.signalstats.YAVG" -f null - 2>&1 | grep YAVG
   ffmpeg -i recording.mp4 -af silencedetect=n=-30dB:d=0.02 -f null - 2>&1 | grep silence_end
   ```
   閃爍影格的 `YAVG` 很高；每個 `silence_end` 是嗶聲開始。兩者的差就是影音偏移。請一併記錄機器、作業系統、SnapTray 版本、解析度、影格率、編碼器與電源模式。
