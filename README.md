<p align="center">
  <img src="resources/icons/snaptray.png" alt="SnapTray logo" width="144" />
</p>

<h1 align="center">SnapTray</h1>

<p align="center">
  <strong>English</strong> | <a href="README.zh-TW.md">繁體中文</a>
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

SnapTray is a Qt 6 screenshot and annotation app for macOS, Windows, and Ubuntu 22.04 X11 beta. It is built for fast desktop workflows: capture a region, explain it instantly, and keep references on screen. Recording and OCR are macOS/Windows only; they are hidden and not included in the Linux beta.

## Why SnapTray

- Capture quickly with magnified selection, window detection, multi-region capture, cursor inclusion, and color picking
- Annotate immediately with arrows, marker, shapes, text, mosaic, step badges, emoji, QR scan, and auto blur; OCR is macOS/Windows only
- Pin screenshots above other windows so references stay visible while you work
- On macOS/Windows, record a full screen source from the tray menu or recording hotkey, with direct start on single-display setups and screen picking on multi-display setups
- Launch repeatable flows from global hotkeys, the tray menu, or the CLI
- Linux beta: Ubuntu 22.04 X11 AppImage; recording and OCR are not shown.

## Built for Real Work

### Capture and mark up in one pass

Press `F2`, drag a region, then copy, save, pin, or blur from the same toolbar. OCR is also available there on macOS/Windows.

> **Note:** Image sharing (upload to a shareable URL) is currently disabled. The Share button is hidden from the capture and pin-window toolbars, so it cannot be triggered from the UI. The underlying share code is intentionally retained, so the feature can be re-enabled in a later build without reimplementing it.

### Draw directly on the desktop

Use `Ctrl+F2 / Cmd+F2` to open Screen Canvas for demos, walkthroughs, presentations, and live explanation.

### Keep references where you need them

Pinned images stay above other apps and support zoom, opacity, rotation, flip, merge/layout controls, and inline annotation.

### Record what matters

On macOS/Windows, start from the tray menu or recording hotkey, choose a screen when needed, then use the floating control bar to monitor duration and stop recording.

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
