---
layout: docs
title: 命令列
description: 官方自動化介面，可用於擷取、錄影、釘選與設定操作。
permalink: /zh-tw/docs/cli/
lang: zh-tw
route_key: docs_cli
doc_group: advanced
doc_order: 1
---

CLI 是 SnapTray 的官方自動化介面。你可以用它從 shell 或 CI 流程執行本地擷取、透過 IPC 控制正在執行的 app，並管理設定。

## 安裝 CLI helper

### macOS

到 Settings > General 使用 CLI 安裝功能，建立 `/usr/local/bin/snaptray`。此操作需要管理員權限。

### Windows

到 Settings > General 使用 CLI 安裝功能，將 SnapTray 執行檔所在目錄加入目前使用者的 `PATH`。安裝或移除後請重新開啟終端機。

### Windows 打包版本

部分安裝方式可能已透過 `PATH` 或 App Execution Alias 提供 `snaptray` 指令，但目前應用程式碼的 canonical 行為仍是 in-app 安裝與移除流程。

## 指令矩陣

| 指令 | 說明 | 是否需要 SnapTray 主程式執行中 |
|---|---|---|
| `full` | 擷取游標所在全螢幕，或透過 `-n` 指定螢幕 | 否 |
| `screen` | 擷取指定螢幕，或用 `--list` 列出螢幕 | 否 |
| `region` | 從指定螢幕擷取 `-r x,y,width,height` 區域 | 否 |
| `gui` | 開啟區域截圖 GUI | 是 |
| `canvas` | 切換 Screen Canvas | 是 |
| `record` | 開始、停止或切換錄影 | 是 |
| `pin` | 釘選圖片檔或剪貼簿圖片 | 是 |
| `config` | 列出、讀取、寫入或重設設定；無參數時開啟設定視窗 | 部分 |

## 指令範例

```bash
# 說明與版本
snaptray --help
snaptray --version
snaptray full --help

# 本地擷取指令
snaptray full                         # 擷取游標所在螢幕
snaptray full -c                      # 全螢幕複製到剪貼簿
snaptray full -d 1000 -o shot.png     # 延遲 1 秒後存檔
snaptray full -n 1 -o screen1.png     # 擷取螢幕 1 到檔案
snaptray full --raw > shot.png        # 將 PNG bytes 輸出到 stdout
snaptray screen --list                # 列出可用螢幕
snaptray screen 0 -c                  # 擷取螢幕 0（位置參數）
snaptray screen -n 1 -o screen1.png   # 擷取螢幕 1（選項參數）
snaptray screen 1 -o screen1.png      # 擷取螢幕 1 到檔案
snaptray region -r 0,0,800,600 -c     # 從螢幕 0 擷取區域到剪貼簿
snaptray region -n 1 -r 100,100,400,300 -o region.png
snaptray region -r 100,100,400,300 -o region.png

# IPC 指令
snaptray gui                          # 開啟區域截圖選擇器
snaptray gui -d 2000                  # 延遲 2 秒開啟
snaptray canvas                       # 切換 Screen Canvas
snaptray record start                 # 開始錄影
snaptray record stop                  # 停止錄影
snaptray record                       # 切換錄影
snaptray record start -n 1            # 在螢幕 1 開始全螢幕錄影
snaptray pin -f image.png             # 釘選圖片檔
snaptray pin -c --center              # 從剪貼簿釘選並置中
snaptray pin -f image.png -x 200 -y 120
snaptray config                       # 開啟設定視窗

# 本地設定指令
snaptray config --list
snaptray config --get hotkey
snaptray config --set files/filenamePrefix SnapTray
snaptray config --reset
```

## 行為補充

- 擷取指令（`full`、`screen`、`region`）預設輸出 PNG。
- `--clipboard` 會改成複製到剪貼簿；`--raw` 會把 PNG bytes 輸出到 stdout。
- `--output` 的優先權高於 `--path`。若兩者都沒給，SnapTray 會依目前設定的截圖資料夾自動產生檔名。
- `screen` 同時支援 `snaptray screen 1` 與 `snaptray screen -n 1`。
- `region` 必須提供 `-r/--region`，使用相對於所選螢幕的 logical pixels，且矩形必須完全落在該螢幕範圍內。
- `record` 只接受 `start`、`stop`、`toggle`。不給 action 時預設是 `toggle`。目前實作中，`-n/--screen` 只會被 `record start` 使用。
- `pin` 必須二選一提供 `--file` 或 `--clipboard`。`--file` 必須是可讀圖片。只有同時提供 `-x` 與 `-y` 才會套用自訂位置，否則仍會置中。
- `config --set` 只接受一個位置參數值。`config --reset` 會清空整個 settings store。

## 回傳碼

| 代碼 | 意義 |
|---|---|
| `0` | 成功 |
| `1` | 一般錯誤 |
| `2` | 無效參數 |
| `3` | 檔案錯誤 |
| `4` | 實例錯誤（主程式未執行） |
| `5` | 錄影錯誤（`CLIResult::Code` 有定義，但目前 CLI 流程不會回傳） |

## 相關文件

- [快速開始](/zh-tw/docs/getting-started/)
- [疑難排解](/zh-tw/docs/troubleshooting/)
- [MCP（僅 Debug Build）](/zh-tw/developer/mcp-debug/) 供 debug-only 本地自動化使用
