---
last_modified_at: 2026-03-24
layout: docs
title: 開發者文件
description: SnapTray 的技術文件主入口，集中整理建置、打包、架構與除錯整合資訊，不再把 GitHub README 撐成長文總表。
permalink: /zh-tw/developer/
lang: zh-tw
route_key: developer_home
nav_data: developer_nav
docs_copy_key: developer
---

這裡是 SnapTray 技術文件的主入口。根 README 會保持產品導向；原本放在 README 與 AGENTS 的技術長文，現在集中整理於此。

## 這裡收錄什麼

- [從原始碼建置](build-from-source.md)：前置需求、腳本、手動 CMake 流程、快取工具與本地建置排錯
- [發佈與打包](release-packaging.md)：DMG / NSIS / MSIX 打包、簽章、公證、Store 提交流程與圖示資產
- [架構總覽](architecture.md)：專案結構、library 邊界、架構模式、平台對照與開發慣例
- [MCP（僅 Debug Build）](mcp-debug.md)：內建 localhost MCP server、tool contract 與整合方式

## 受眾分工

- GitHub `README`：產品著陸頁、下載入口與高階工作流概覽
- `/docs/*`：使用者教學、功能參考、疑難排解、FAQ 與 CLI 使用方式
- `/developer/*`：貢獻者與 agent 需要的技術參考

## 真實來源原則

- 產品介紹與價值主張放在 README 與網站首頁
- 使用者操作細節放在 `/docs/*`
- 建置、打包、架構、debug-only 整合資訊放在 `/developer/*`
- `AGENTS.md` 只保留精簡操作索引，避免重複維護會漂移的長清單

## 建議閱讀順序

1. [從原始碼建置](build-from-source.md)
2. [架構總覽](architecture.md)
3. [發佈與打包](release-packaging.md)
4. 需要 debug 自動化時再看 [MCP（僅 Debug Build）](mcp-debug.md)

## 相關使用者文件

- [快速開始](../docs/getting-started.md)
- [命令列](../docs/cli.md)
- [疑難排解](../docs/troubleshooting.md)
- [常見問題](../docs/faq.md)
