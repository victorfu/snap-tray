# SnapTray

**English** | [繁體中文](README.zh-TW.md)

SnapTray is a tray-native screenshot and recording tool for macOS and Windows. It is built for fast capture workflows: press `F2` to grab a region, `Ctrl+F2 / Cmd+F2` to enter Screen Canvas, annotate immediately, pin references on screen, and export high-quality recordings without leaving the desktop.

## Why SnapTray

- **Capture fast**: precision region selection with magnifier, window detection, multi-region capture, cursor inclusion, and color picking
- **Annotate clearly**: arrow, pencil, marker, shapes, text, mosaic, step badges, emoji, undo/redo, OCR, QR scan, and auto blur
- **Stay in context**: pin screenshots above other windows, resize and rotate them, and annotate directly on pinned images
- **Record what matters**: capture full screen or a selected region and export MP4, GIF, or WebP with optional audio
- **Automate repeat work**: use global hotkeys, the CLI, and debug-build MCP tooling for local automation flows

## Core Workflows

### Region Capture

Start with `F2`, drag a region, annotate it, then copy, save, pin, share, OCR, blur, or start a recording from the same toolbar.

### Screen Canvas

Use `Ctrl+F2 / Cmd+F2` to draw directly on the desktop with live presentation tools such as marker, shape, text, step badges, and laser pointer.

### Pin Window

Keep screenshots visible while working. Pins support zoom, opacity, rotation, flip, merge/layout controls, and a dedicated inline annotation toolbar.

### Recording

Start recording from the tray or the capture toolbar. Use the floating control bar to pause, resume, stop, or cancel, then export in the format that fits your use case.

## Platforms

- macOS 14+
- Windows 10+

## Docs & Releases

- Releases: [GitHub Releases](https://github.com/victorfu/snap-tray/releases)
- User docs: [Documentation Home](docs/docs/index.md)
- Tutorials: [Tutorial Hub](docs/docs/tutorials/index.md)
- CLI reference: [CLI](docs/docs/cli.md)
- Troubleshooting: [Troubleshooting](docs/docs/troubleshooting.md)
- Developer docs: [Developer Docs](docs/developer/index.md)

## Build from Source

The full build, packaging, signing, architecture, and debug-only MCP documentation now lives in [Developer Docs](docs/developer/index.md).

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

For prerequisites, manual CMake flows, packaging, notarization, Store submission, and repository architecture, use:

- [Build from Source](docs/developer/build-from-source.md)
- [Release & Packaging](docs/developer/release-packaging.md)
- [Architecture Overview](docs/developer/architecture.md)
- [MCP (Debug Builds)](docs/developer/mcp-debug.md)

## License

MIT License
