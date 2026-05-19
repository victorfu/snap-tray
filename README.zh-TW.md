<p align="center">
  <img src="resources/icons/snaptray.png" alt="SnapTray logo" width="144" />
</p>

<h1 align="center">SnapTray</h1>

<p align="center">
  <a href="README.md">English</a> | <strong>繁體中文</strong>
</p>

---

<p align="center">
  在桌面上完成截圖、標註與釘選，不必切換工具；錄影支援 macOS/Windows。
</p>

<p align="center">
  macOS 14+ · Windows 10+ · Ubuntu 22.04 X11 beta
</p>

<p align="center">
  <a href="https://github.com/victorfu/snap-tray/releases">下載</a> ·
  <a href="docs/zh-tw/docs/index.md">文件</a> ·
  <a href="docs/zh-tw/docs/tutorials/index.md">教學</a>
</p>

SnapTray 是一款支援 macOS、Windows 與 Ubuntu 22.04 X11 beta 的 Qt 6 截圖與標註工具。它是為了快速桌面工作流而設計：擷取畫面、立即說明重點、把參考圖釘在畫面上，或直接把同一個畫面輸出成可分享的圖片。錄影與 OCR 僅支援 macOS/Windows；Linux beta 會隱藏且不包含這些功能。

## 為什麼選擇 SnapTray

- 擷取更快：支援放大鏡精準選取、視窗偵測、多區域擷取、包含游標與取色
- 標註更快：內建箭頭、螢光筆、形狀、文字、馬賽克、步驟標記、貼圖、QR 掃描與自動模糊；OCR 僅支援 macOS/Windows
- 工作不中斷：可把截圖釘選在其他視窗上方，讓參考畫面持續可見
- 錄影更清楚：在 macOS/Windows 可從托盤選單或 CLI 錄製完整螢幕來源，單螢幕直接開始，多螢幕先選擇螢幕
- 重複流程更順手：可從全域快捷鍵、托盤選單或 CLI 啟動常用流程
- Linux beta：Ubuntu 22.04 X11 AppImage；不顯示錄影與 OCR。

## 為真實工作流而設計

### 一次完成擷取與標註

按下 `F2` 後拖曳選區，即可在同一條工具列完成複製、存檔、釘選、分享與自動模糊；macOS/Windows 也可使用 OCR。

### 直接在桌面上說明

按 `Ctrl+F2 / Cmd+F2` 開啟螢幕畫布，適合示範、教學、簡報與即時說明。

### 把參考圖留在眼前

釘選後的圖片會固定在其他 App 上方，並支援縮放、透明度、旋轉、翻轉、合併/版面控制與內嵌標註。

### 錄下真正重要的內容

在 macOS/Windows 可從托盤選單或 CLI 啟動錄影；若是多螢幕，先選擇螢幕，再透過浮動控制列查看時間並停止錄影。

## 進一步了解

- 發佈版本：[GitHub Releases](https://github.com/victorfu/snap-tray/releases)
- 使用者文件：[文件首頁](docs/zh-tw/docs/index.md)
- 教學總覽：[教學總覽](docs/zh-tw/docs/tutorials/index.md)
- CLI 參考：[命令列](docs/zh-tw/docs/cli.md)
- 疑難排解：[疑難排解](docs/zh-tw/docs/troubleshooting.md)

## 開發者資訊

若你要從原始碼建置 SnapTray，或要直接參與開發，請從 [開發者文件](docs/zh-tw/developer/index.md) 開始。

快速入口：

```bash
# macOS/Linux beta
./scripts/build.sh
./scripts/run-tests.sh
```

```batch
REM Windows
scripts\build.bat
scripts\run-tests.bat
```

更多開發文件：

- [從原始碼建置](docs/zh-tw/developer/build-from-source.md)
- [發佈與打包](docs/zh-tw/developer/release-packaging.md)
- [架構總覽](docs/zh-tw/developer/architecture.md)

## 授權條款

MIT License
