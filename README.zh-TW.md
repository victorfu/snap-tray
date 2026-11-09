<p align="center">
  <img src="resources/icons/snaptray.png" alt="SnapTray logo" width="144" />
</p>

<h1 align="center">SnapTray</h1>

<p align="center">
  <a href="README.md">English</a> | <strong>繁體中文</strong> | <a href="README.ja.md">日本語</a> | <a href="README.ko.md">한국어</a> | <a href="README.th.md">ไทย</a>
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

SnapTray 是一款支援 macOS、Windows 與 Ubuntu 22.04 X11 beta 的 Qt 6 截圖與標註工具。它是為了快速桌面工作流而設計：擷取畫面、立即說明重點、把參考圖釘在畫面上。錄影與 OCR 僅支援 macOS/Windows；Linux beta 會隱藏且不包含這些功能。

## 為什麼選擇 SnapTray

- 擷取更快：支援放大鏡精準選取、視窗偵測、多區域擷取、包含游標與取色
- 標註更快：內建箭頭、螢光筆、形狀、文字、馬賽克、步驟標記、貼圖、QR 掃描與自動模糊；OCR 僅支援 macOS/Windows
- 工作不中斷：可把截圖釘選在其他視窗上方，讓參考畫面持續可見
- 錄影更清楚：在 macOS/Windows 可從托盤選單或錄影快捷鍵錄製完整螢幕來源，單螢幕直接開始，多螢幕先選擇螢幕
- 重複流程更順手：可從全域快捷鍵、托盤選單或 CLI 啟動常用流程
- Linux beta：Ubuntu 22.04 X11 AppImage；不顯示錄影與 OCR。

## 為真實工作流而設計

### 一次完成擷取與標註

按下 `F2` 後拖曳選區，即可在同一條工具列完成複製、存檔、釘選與自動模糊；macOS/Windows 也可使用 OCR。

### 直接在桌面上說明

按 `Ctrl+F2 / Cmd+F2` 開啟螢幕畫布，適合示範、教學、簡報與即時說明。

### 把參考圖留在眼前

釘選後的圖片會固定在其他 App 上方，並支援縮放、透明度、旋轉、翻轉、合併/版面控制與內嵌標註。

### 錄下真正重要的內容

在 macOS/Windows 可從托盤選單或錄影快捷鍵啟動錄影；若是多螢幕，先選擇螢幕，再透過浮動控制列查看時間並停止錄影。

## Linux：AppImageLauncher 版本過舊

舊版 AppImageLauncher（包含 Ubuntu PPA 的 2022 年版本）可能導致 SnapTray AppImage 無法啟動，出現 `fuse: memory allocation failed`、`Can't open squashfs image: Bad address` 或 `execv error: No such file or directory`。這些訊息可能是 [AppImageLauncher 相容性問題](https://github.com/AppImage/type2-runtime/issues/121)，不一定代表記憶體不足。

如果已安裝 AppImageLauncher，請從[官方發佈頁面](https://github.com/TheAssassin/AppImageLauncher/releases)下載適合系統的 `.deb` 更新；Ubuntu x86_64 請選擇 `amd64.deb`。舊 PPA 可能仍提供有問題的版本，請依照[官方 Ubuntu 安裝說明](https://github.com/TheAssassin/AppImageLauncher/wiki/Install-on-Ubuntu-or-Debian)操作。

更新後若啟動看似卡住，可先在該終端機按 `Ctrl+C`，再嘗試重新載入使用者服務，不必先重新開機：

```bash
systemctl --user daemon-reload
systemctl --user restart appimagelauncherd.service
# 將 <version> 與路徑替換為實際下載的 AppImage。
timeout 15s ./SnapTray-<version>-x86_64.AppImage --version
```

`Created symlink ... appimagelauncherd.service` 是正常的服務設定訊息。版本檢查成功後，即可正常啟動 SnapTray；若不想占用終端機，可在背景執行：

```bash
nohup ./SnapTray-<version>-x86_64.AppImage > /tmp/snaptray.log 2>&1 &
```

SnapTray 啟動後會常駐系統列，請尋找托盤圖示，不會自動開啟主視窗。如果重啟服務仍無法解決，請參考[解壓後執行的替代方式](docs/developer/release-packaging.md#linux)。

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
