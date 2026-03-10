---
layout: docs
title: 命令列
description: 官方自動化介面，可透過腳本控制擷取、錄影與釘選。
permalink: /zh-tw/docs/cli/
lang: zh-tw
route_key: docs_cli
doc_group: advanced
doc_order: 1
---

CLI 是 SnapTray 的官方自動化介面。你可以用它來編寫擷取腳本、控制錄影，以及從任何 shell 或 CI 流程管理釘選視窗。

## 安裝 CLI helper

在 Settings > General 使用 CLI 安裝功能，啟用 `snaptray` 指令。

## 指令列表

| 指令 | 說明 | 是否需要 SnapTray 主程式執行中 |
|---|---|---|
| `full` | 擷取全螢幕 | 否 |
| `screen` | 擷取指定螢幕 | 否 |
| `region` | 擷取指定區域 | 否 |
| `gui` | 開啟區域截圖 GUI | 是 |
| `canvas` | 開啟/切換 Screen Canvas | 是 |
| `record` | `start` / `stop` / `toggle` 錄影 | 是 |
| `pin` | 從檔案或剪貼簿建立釘選 | 是 |
| `config` | 列出/讀取/寫入/重設設定（單獨 `config` 會開啟設定視窗） | 部分 |

## 指令範例

```bash
# 全域
snaptray --help
snaptray --version

# 本地擷取指令
snaptray full -c
snaptray screen --list
snaptray screen 1 -o screen1.png
snaptray region -r 100,100,400,300 -o region.png

# IPC 指令
snaptray gui -d 1500
snaptray canvas
snaptray record start -n 0
snaptray pin -f image.png --center

# 設定指令
snaptray config --list
snaptray config --get hotkey
snaptray config --set hotkey F6
snaptray config --reset
```

## 回傳碼

可在腳本或 CI 流程中以 exit code 判定成功/失敗並安排重試。
