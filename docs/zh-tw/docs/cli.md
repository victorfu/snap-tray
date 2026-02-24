---
layout: docs
title: 命令列
description: 使用 CLI 進行本地擷取與 IPC 控制，建立自動化流程。
permalink: /zh-tw/docs/cli/
lang: zh-tw
route_key: docs_cli
doc_group: advanced
doc_order: 1
---

## 安裝 CLI helper

在 Settings > General 使用 CLI 安裝功能，啟用 `snaptray` 指令。

## 指令群組

### 本地擷取指令

不需主程式常駐即可執行指定擷取動作。

### IPC 指令

向正在執行中的 SnapTray 發送 GUI、畫布、釘選、錄影命令。

### 設定指令

可在腳本中讀取或更新設定值。

## 指令範例

```bash
snaptray --help
snaptray region --copy
snaptray screen --save
snaptray gui --show
snaptray config --get files.screenshotPath
```

## 回傳碼

可在腳本或 CI 流程中以 exit code 判定成功/失敗並安排重試。
