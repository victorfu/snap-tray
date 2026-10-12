<p align="center">
  <img src="resources/icons/snaptray.png" alt="SnapTray logo" width="144" />
</p>

<h1 align="center">SnapTray</h1>

<p align="center">
  <strong>English</strong> | <a href="README.zh-TW.md">繁體中文</a> | <a href="README.ja.md">日本語</a> | <a href="README.ko.md">한국어</a> | <a href="README.th.md">ไทย</a>
</p>

---

<p align="center">
  Capture, annotate, and pin without leaving the desktop; recording is available on macOS/Windows.
</p>

<p align="center">
  macOS 14+ · Windows 10+ · Ubuntu 22.04 X11 beta
</p>

<p align="center">
  <a href="https://github.com/victorfu/snap-tray/releases">Download</a> ·
  <a href="docs/docs/index.md">Documentation</a> ·
  <a href="docs/docs/tutorials/index.md">Tutorials</a>
</p>

SnapTray is a Qt 6 screenshot and annotation app for macOS, Windows, and Ubuntu 22.04 X11 beta. It is built for fast desktop workflows: capture a region, explain it instantly, and keep references on screen. Recording is available on macOS, Windows, and Linux X11; OCR remains macOS/Windows only.

## Why SnapTray

- Capture quickly with magnified selection, window detection, multi-region capture, cursor inclusion, and color picking
- Annotate immediately with arrows, marker, shapes, text, mosaic, step badges, emoji, QR scan, and auto blur; OCR is macOS/Windows only
- Pin screenshots above other windows so references stay visible while you work
- On macOS/Windows, record a full screen source from the tray menu or recording hotkey, with direct start on single-display setups and screen picking on multi-display setups
- Launch repeatable flows from global hotkeys, the tray menu, or the CLI
- Linux beta: Ubuntu 22.04 X11 AppImage with FFmpeg recording and PulseAudio/PipeWire audio; OCR is not shown.

## Built for Real Work

### Capture and mark up in one pass

Press `F2`, drag a region, then copy, save, pin, or blur from the same toolbar. OCR is also available there on macOS/Windows.

### Draw directly on the desktop

Use `Ctrl+F2 / Cmd+F2` to open Screen Canvas for demos, walkthroughs, presentations, and live explanation.

### Keep references where you need them

Pinned images stay above other apps and support zoom, opacity, rotation, flip, merge/layout controls, and inline annotation.

### Record what matters

On macOS/Windows, start from the tray menu or recording hotkey, choose a screen when needed, then use the floating control bar to monitor duration and stop recording.

## Linux: outdated AppImageLauncher

Older AppImageLauncher installations (including the 2022 Ubuntu PPA build) can prevent SnapTray's AppImage from starting, with errors such as `fuse: memory allocation failed`, `Can't open squashfs image: Bad address`, or `execv error: No such file or directory`. These messages can indicate an [AppImageLauncher compatibility issue](https://github.com/AppImage/type2-runtime/issues/121), rather than insufficient memory.

If AppImageLauncher is installed, update it using the appropriate `.deb` from the [official releases](https://github.com/TheAssassin/AppImageLauncher/releases). For Ubuntu x86_64, choose the `amd64.deb` package. The old PPA may still offer the affected version; follow the [official Ubuntu installation instructions](https://github.com/TheAssassin/AppImageLauncher/wiki/Install-on-Ubuntu-or-Debian).

After updating, if launching appears to hang, press `Ctrl+C` in that terminal and try reloading the user service without rebooting:

```bash
systemctl --user daemon-reload
systemctl --user restart appimagelauncherd.service
# Replace <version> and the path with your downloaded AppImage.
timeout 15s ./SnapTray-<version>-x86_64.AppImage --version
```

The `Created symlink ... appimagelauncherd.service` message is normal service setup output. Once the version check succeeds, launch SnapTray normally, or keep the terminal free with:

```bash
nohup ./SnapTray-<version>-x86_64.AppImage > /tmp/snaptray.log 2>&1 &
```

SnapTray runs in the system tray; look for its tray icon rather than a main window. If the service restart does not resolve the issue, see the [extraction workaround](docs/developer/release-packaging.md#linux).

## Learn More

- Releases: [GitHub Releases](https://github.com/victorfu/snap-tray/releases)
- User docs: [Documentation Home](docs/docs/index.md)
- Tutorials: [Tutorial Hub](docs/docs/tutorials/index.md)
- CLI: [CLI Reference](docs/docs/cli.md)
- Troubleshooting: [Troubleshooting](docs/docs/troubleshooting.md)

## For Developers

If you want to build SnapTray from source or work on the codebase, start with [Developer Docs](docs/developer/index.md).

Quick entry points:

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

More developer references:

- [Build from Source](docs/developer/build-from-source.md)
- [Release & Packaging](docs/developer/release-packaging.md)
- [Architecture Overview](docs/developer/architecture.md)

## License

MIT License
