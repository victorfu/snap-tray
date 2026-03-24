---
layout: docs
title: MCP（僅 Debug Build）
description: 內建 localhost MCP server 只在 Debug build 可用，主要供本地自動化與開發者工具整合使用。
permalink: /zh-tw/developer/mcp-debug/
lang: zh-tw
route_key: developer_mcp_debug
nav_data: developer_nav
docs_copy_key: developer
---

## 可用性

SnapTray 的 MCP server 是 **僅限 Debug build** 的能力。

- `CMakeLists.txt` 只在 `Debug` build 定義 `SNAPTRAY_ENABLE_MCP`
- 主程式只有在編進這個 feature flag 時才會啟動 MCP server
- Settings 裡的 MCP 開關也只在 debug build 顯示

因此 MCP 文件應該放在 developer docs，而不是產品導向的 README。

## 傳輸與端點

- 傳輸：streamable HTTP
- Endpoint：`POST http://127.0.0.1:39200/mcp`
- `GET /mcp`：回傳 `405 Method Not Allowed`
- JSON-RPC methods：`initialize`、`notifications/initialized`、`tools/list`、`tools/call`、`ping`

## 安全模型

- 僅 bind 到 `127.0.0.1`
- 目前 V1 對 localhost 不要求 bearer token
- 設計目標是本地開發者工具，不是遠端多使用者服務

## 工具列表

| 工具 | 輸入 | 輸出 |
|---|---|---|
| `capture_screenshot` | `screen?`、`region? {x,y,width,height}`、`delay_ms?`、`output_path?` | `file_path`、`width`、`height`、`screen_index`、`created_at` |
| `pin_image` | `image_path`、`x?`、`y?`、`center?` | `accepted` |
| `share_upload` | `image_path`、`password?` | `url`、`expires_at`、`protected` |

若省略 `capture_screenshot.output_path`，SnapTray 會寫入暫存目錄。

## 執行時行為

- MCP server 直接跑在主程式 `snaptray` 內
- 不需要額外的 MCP binary
- 如果埠號綁定失敗，主程式會記錄 warning，並顯示使用者可見的提示訊息

## ChatGPT Desktop 設定範例

1. 開啟「Connect to a custom MCP」
2. 選擇 `Streamable HTTP`
3. 任意命名，例如 `snaptray-local`
4. URL 填 `http://127.0.0.1:39200/mcp`
5. localhost 可留空 bearer token
6. 確認 SnapTray 的 debug build 正在執行

## 來源對照

- 傳輸與協定：`src/mcp/`
- 啟動整合：`src/MainApplication.cpp`
- 開關狀態：`src/settings/MCPSettingsManager.cpp`

## 相關文件

- [從原始碼建置](build-from-source.md)
- [命令列](../docs/cli.md)
