<p align="center">
  <img src="resources/icons/snaptray.png" alt="SnapTray logo" width="144" />
</p>

<h1 align="center">SnapTray</h1>

<p align="center">
  <strong>English</strong> | <a href="README.zh-TW.md">繁體中文</a>
</p>

---

<p align="center">
  Capture, annotate, pin, and record your screen without leaving the desktop.
</p>

<p align="center">
  macOS 14+ · Windows 10+
</p>

<p align="center">
  <a href="https://github.com/victorfu/snap-tray/releases">Download</a> ·
  <a href="docs/docs/index.md">Documentation</a> ·
  <a href="docs/docs/tutorials/index.md">Tutorials</a>
</p>

SnapTray is a tray-native screenshot and screen recording app for macOS and Windows. It is built for fast desktop workflows: capture a region, explain it instantly, keep references on screen, and turn the same moment into a shareable image or video.

## Why SnapTray

- Capture quickly with magnified selection, window detection, multi-region capture, cursor inclusion, and color picking
- Annotate immediately with arrows, marker, shapes, text, mosaic, step badges, emoji, OCR, QR scan, and auto blur
- Pin screenshots above other windows so references stay visible while you work
- Record a full screen source from the tray menu or CLI, with direct start on single-display setups and screen picking on multi-display setups
- Launch repeatable flows from global hotkeys, the tray menu, or the CLI

## Built for Real Work

### Capture and mark up in one pass

Press `F2`, drag a region, then copy, save, pin, share, OCR, or blur from the same toolbar.

### Draw directly on the desktop

Use `Ctrl+F2 / Cmd+F2` to open Screen Canvas for demos, walkthroughs, presentations, and live explanation.

### Keep references where you need them

Pinned images stay above other apps and support zoom, opacity, rotation, flip, merge/layout controls, and inline annotation.

### Record what matters

Start from the tray menu or CLI, choose a screen when needed, then use the floating control bar to monitor duration and stop recording.

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
# macOS
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
- [MCP (Debug Builds)](docs/developer/mcp-debug.md)

## License

MIT License
