# SnapTray

[English](README.md) | **繁體中文**

SnapTray 是一款常駐系統托盤的截圖與錄影工具，支援 macOS 與 Windows。它的核心目標是讓你在桌面上完成整條工作流：按 `F2` 擷取區域、按 `Ctrl+F2 / Cmd+F2` 進入螢幕畫布，直接標註、釘選參考圖、輸出高品質錄影，不必切換一堆工具。

## 為什麼用 SnapTray

- **擷取更快**：提供放大鏡精準選取、視窗偵測、多區域擷取、包含游標與取色能力
- **標註更清楚**：箭頭、鉛筆、螢光筆、形狀、文字、馬賽克、步驟標記、貼圖、Undo/Redo、OCR、QR 掃描與自動模糊
- **工作不離開上下文**：可把截圖釘選在其他視窗上方，支援縮放、旋轉，還能直接在 pin window 裡標註
- **錄影更完整**：可從托盤或擷取工具列啟動錄影，輸出 MP4、GIF、WebP，並支援可用平台上的音訊
- **重複工作可自動化**：提供全域快捷鍵、CLI，以及 debug build 專用的 MCP 本地自動化介面

## 核心工作流

### 區域截圖

按 `F2` 後拖曳選區，接著在同一條工具列完成標註、複製、存檔、釘選、分享、OCR、自動模糊，或直接開始錄影。

### 螢幕畫布

按 `Ctrl+F2 / Cmd+F2` 直接在桌面上畫。適合講解、簡報與即時示範，包含 marker、shape、text、step badge 與 laser pointer。

### 釘選視窗

把截圖固定在畫面上當參考。支援縮放、透明度、旋轉、翻轉、合併/版面控制，以及獨立的標註工具列。

### 錄影

可從托盤或擷取工具列啟動。透過浮動控制列暫停、恢復、停止或取消，再輸出成最適合的格式。

## 支援平台

- macOS 14+
- Windows 10+

## 文件與發佈

- 發佈版本：[GitHub Releases](https://github.com/victorfu/snap-tray/releases)
- 使用者文件：[文件首頁](docs/zh-tw/docs/index.md)
- 教學總覽：[教學總覽](docs/zh-tw/docs/tutorials/index.md)
- CLI 參考：[命令列](docs/zh-tw/docs/cli.md)
- 疑難排解：[疑難排解](docs/zh-tw/docs/troubleshooting.md)
- 開發者文件：[開發者文件](docs/zh-tw/developer/index.md)

## 從原始碼建置

完整的建置、打包、簽章、架構與 debug-only MCP 文件已移到 [開發者文件](docs/zh-tw/developer/index.md)。

快速入口：

```bash
# macOS
./scripts/build.sh
./scripts/run-tests.sh
```

```batch
REM Windows
scripts\build.bat
scripts\run-tests.bat
```

若要看前置需求、手動 CMake、打包、公證、Store 提交與 repo 架構，請直接前往：

- [從原始碼建置](docs/zh-tw/developer/build-from-source.md)
- [發佈與打包](docs/zh-tw/developer/release-packaging.md)
- [架構總覽](docs/zh-tw/developer/architecture.md)
- [MCP（僅 Debug Build）](docs/zh-tw/developer/mcp-debug.md)

## 授權條款

MIT License
