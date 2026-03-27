---
last_modified_at: 2026-03-24
layout: docs
title: Architecture Overview
description: Repository structure, library boundaries, subsystem responsibilities, platform abstractions, and contributor-facing development conventions.
permalink: /developer/architecture/
lang: en
route_key: developer_architecture
nav_data: developer_nav
docs_copy_key: developer
---

## Tech stack

- Language: C++17
- Framework: Qt 6
- Build system: CMake + Ninja
- UI technologies: Qt Widgets plus QML overlays and settings surfaces
- Packaging targets: macOS DMG, Windows NSIS, Windows MSIX

## Repository structure

```text
snap-tray/
├── include/                    # Public headers and subsystem interfaces
│   ├── annotation/             # Annotation context and host adapter
│   ├── annotations/            # Annotation item types
│   ├── beautify/               # Beautify interfaces and settings
│   ├── capture/                # Capture engine interfaces
│   ├── cli/                    # CLI handler, commands, IPC protocol
│   ├── colorwidgets/           # Custom color widgets
│   ├── cursor/                 # Cursor management
│   ├── detection/              # Face / table / credential detection
│   ├── encoding/               # Video, GIF, WebP encoders
│   ├── history/                # History data models and helpers
│   ├── hotkey/                 # Hotkey manager and action types
│   ├── mcp/                    # Debug-only MCP server contracts
│   ├── metal/                  # Apple Metal capture / rendering helpers
│   ├── pinwindow/              # Pin window components
│   ├── platform/               # Platform abstraction layer
│   ├── qml/                    # QML bridges and view models
│   ├── region/                 # Region selector components
│   ├── settings/               # Settings managers
│   ├── share/                  # Share upload client
│   ├── tools/                  # Tool registry and handlers
│   ├── ui/                     # Cross-cutting UI helpers and theme
│   ├── update/                 # Auto-update classes
│   ├── utils/                  # Utility helpers
│   ├── video/                  # Playback and recording UI
│   └── widgets/                # Custom widgets and dialogs
├── src/                        # Implementations mirroring include/
│   ├── qml/components/         # Shared QML blocks
│   ├── qml/controls/           # Reusable controls
│   ├── qml/dialogs/            # Dialog surfaces
│   ├── qml/panels/             # Capture overlay panels
│   ├── qml/recording/          # Recording overlays
│   ├── qml/settings/           # Settings pages
│   ├── qml/tokens/             # QML design tokens
│   └── qml/toolbar/            # Floating toolbar surfaces
├── tests/                      # Qt Test suites by subsystem
├── docs/                       # Website, user docs, and developer docs
├── packaging/                  # macOS and Windows packaging
├── resources/                  # Icons, cascades, shaders, and resources.qrc
├── scripts/                    # Build, run, test, and i18n scripts
├── translations/               # Translation sources and outputs
└── CMakeLists.txt              # Top-level configuration and dependency setup
```

## Library architecture

SnapTray uses a modular static-library-oriented structure:

1. `snaptray_core`: annotations, settings, cursor management, utilities, CLI-adjacent shared logic
2. `snaptray_colorwidgets`: custom color picking UI
3. `snaptray_algorithms`: detection and image analysis
4. `snaptray_platform`: platform-specific capture, encoding, playback, install source, and OS integration
5. `snaptray_ui`: region selector, pin windows, recording workflow, toolbar, settings surfaces
6. `snaptray_mcp`: debug-only MCP transport and tools
7. `SnapTray`: main executable

## Architecture patterns

### Settings managers are the preferred configuration boundary

Use subsystem-specific settings managers instead of ad-hoc `QSettings` access.

- `AnnotationSettingsManager`
- `FileSettingsManager`
- `PinWindowSettingsManager`
- `AutoBlurSettingsManager`
- `OCRSettingsManager`
- `WatermarkSettingsManager`
- `RecordingSettingsManager`
- `UpdateSettingsManager`
- `BeautifySettingsManager`
- `RegionCaptureSettingsManager`
- `LanguageManager`
- `MCPSettingsManager`

### Hotkey registration is centralized

Use `HotkeyManager` rather than constructing `QHotkey` objects directly in feature code. Actions are grouped by category in `HotkeyAction`.

### Tool behavior is data-driven

Tools are defined by `ToolId`, `ToolDefinition`, handlers, and the `ToolRegistry`. Prefer lookup tables and capability maps over large `switch` statements.

### Platform abstraction is explicit

Use `PlatformFeatures` and the platform-specific implementation layer instead of sprinkling OS checks across feature code.

### Floating glass panels use shared rendering helpers

Use `GlassRenderer` and the existing toolbar style configuration rather than local one-off panel rendering logic.

## Platform-specific code map

| Feature | Windows | macOS |
|---|---|---|
| Screen capture | `DXGICaptureEngine_win.cpp` | `SCKCaptureEngine_mac.mm` |
| Video encoding | `MediaFoundationEncoder.cpp` | `AVFoundationEncoder.mm` |
| Audio capture | `WASAPIAudioCaptureEngine_win.cpp` | `CoreAudioCaptureEngine_mac.mm` |
| Video playback | `MediaFoundationPlayer_win.cpp` | `AVFoundationPlayer_mac.mm` |
| Window detection | `WindowDetector_win.cpp` | `WindowDetector.mm` |
| OCR | `OCRManager_win.cpp` | `OCRManager.mm` |
| Window level | `WindowLevel_win.cpp` | `WindowLevel_mac.mm` |
| Platform features | `PlatformFeatures_win.cpp` | `PlatformFeatures_mac.mm` |
| Auto-launch | `AutoLaunchManager_win.cpp` | `AutoLaunchManager_mac.mm` |
| Install source | `InstallSourceDetector_win.cpp` | `InstallSourceDetector_mac.mm` |
| PATH utilities | `PathEnvUtils_win.cpp` | N/A |
| Image color space | N/A | `ImageColorSpaceHelper_mac.mm` |

## Key components

### Core managers

- `CaptureManager`: entry point for region capture workflow
- `PinWindowManager`: lifecycle of floating pin windows
- `ScreenCanvasManager`: full-screen annotation sessions
- `RecordingManager`: recording state machine
- `SingleInstanceGuard`: single-instance enforcement
- `HotkeyManager`: global hotkey registration
- `CLIHandler`: command parsing and IPC dispatch
- `UpdateCoordinator`: native update orchestration

### Region selector subsystem

Important extracted parts under `src/region/`:

- `SelectionStateManager`
- `MagnifierPanel`
- `UpdateThrottler`
- `TextAnnotationEditor`
- `ShapeAnnotationEditor`
- `RegionExportManager`
- `RegionInputHandler`
- `RegionPainter`
- `MultiRegionManager`
- `RegionToolbarHandler`
- `RegionSettingsHelper`
- `SelectionDirtyRegionPlanner`
- `SelectionResizeHelper`
- `CaptureShortcutHintsOverlay`

### Pin window subsystem

Important extracted parts under `src/pinwindow/`:

- `ResizeHandler`
- `UIIndicators`
- `PinWindowPlacement`
- `RegionLayoutManager`
- `RegionLayoutRenderer`
- `PinMergeHelper`
- `PinHistoryStore`
- `PinHistoryWindow`

### Recording subsystem

- `RecordingRegionSelector`
- `RecordingControlBar`
- `RecordingBoundaryOverlay`
- `RecordingInitTask`
- `RecordingRegionNormalizer`

### CLI subsystem

Commands under `include/cli/commands/` include:

- `FullCommand`
- `ScreenCommand`
- `RegionCommand`
- `GuiCommand`
- `CanvasCommand`
- `RecordCommand`
- `PinCommand`
- `ConfigCommand`

## Development guidelines

### Logging

| Function | Purpose | Release behavior |
|---|---|---|
| `qDebug()` | Development diagnostics | Suppressed |
| `qWarning()` | Hardware or API failures | Emitted |
| `qCritical()` | Critical failures | Emitted |

### Code style

- Use C++17 features where they clarify the code
- Prefer `auto` for complex types and explicit types for simple primitives
- Use `nullptr` instead of `NULL`
- Prefer `QPoint` / `QRect` over loose coordinate tuples

### Testing

- Test files use `tests/<Component>/tst_<Name>.cpp`
- The suite uses Qt Test primitives such as `QCOMPARE` and `QVERIFY`
- Tests are organized by subsystem folders including `Annotations`, `Beautify`, `CLI`, `Cursor`, `Detection`, `Encoding`, `Hotkey`, `IPC`, `MCP`, `PinWindow`, `Qml`, `RecordingManager`, `RegionSelector`, `ScreenCanvas`, `Settings`, `Share`, `Tools`, `Update`, and `Utils`

### Common patterns to avoid

- Direct `QSettings` access where a settings manager already exists
- Large repetitive `switch` statements where a lookup table is clearer
- Inline magic numbers without named constants

## Related docs

- [Build from Source](build-from-source.md)
- [Release & Packaging](release-packaging.md)
- [CLI](../docs/cli.md)
