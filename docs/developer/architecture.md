---
last_modified_at: 2026-09-25
layout: docs
title: Architecture Overview
seo_title: "SnapTray Architecture: Subsystems, Boundaries and Layers"
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
- Packaging targets: macOS DMG, Windows NSIS, Windows MSIX, Linux AppImage beta

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
│   ├── metal/                  # Apple Metal capture / rendering helpers
│   ├── pinwindow/              # Pin window components
│   ├── platform/               # Platform abstraction layer
│   ├── qml/                    # QML bridges and view models
│   ├── region/                 # Region selector components
│   ├── settings/               # Settings managers
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
├── packaging/                  # Platform packaging
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
6. `SnapTray`: main executable

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

### Hotkey registration is centralized

Use `HotkeyManager` rather than constructing `QHotkey` objects directly in feature code. Actions are grouped by category in `HotkeyAction`.

### Tool behavior is data-driven

Tools are defined by `ToolId`, `ToolDefinition`, handlers, and the `ToolRegistry`. Prefer lookup tables and capability maps over large `switch` statements.

### Platform abstraction is explicit

Use `PlatformFeatures` and the platform-specific implementation layer instead of sprinkling OS checks across feature code.

`QtQuickBackendPolicy` selects the Qt Quick rendering backend before application startup. Linux beta uses the software scene graph to avoid OpenGL initialization and synchronization delays when capture toolbars appear; Windows 10 also uses software rendering, while macOS and Windows 11 retain the platform default. This policy affects QML UI rendering, not screenshot pixels or annotation rendering.

### Floating glass panels use shared rendering helpers

Use `GlassRenderer` and the existing toolbar style configuration rather than local one-off panel rendering logic.

## Platform-specific code map

| Feature | Windows | macOS | Linux beta |
|---|---|---|---|
| Screen capture | `DXGICaptureEngine_win.cpp`; `WGCCaptureEngine_win.cpp` (experimental, `recording/windowsGraphicsCapture`) | `SCKCaptureEngine_mac.mm` | `QtCaptureEngine.cpp` on X11 |
| Video encoding | `MediaFoundationEncoder.cpp` (+ `D3D11Nv12Converter_win.cpp` for GPU frames) | `AVFoundationEncoder.mm` | Not supported |
| Audio capture | `WASAPIAudioCaptureEngine_win.cpp` | `CoreAudioCaptureEngine_mac.mm` | Not supported |
| Video playback | `MediaFoundationPlayer_win.cpp` | `AVFoundationPlayer_mac.mm` | Not supported |
| Window detection | `WindowDetector_win.cpp` | `WindowDetector.mm` | Not supported |
| OCR | `OCRManager_win.cpp` | `OCRManager.mm` | Not supported |
| Window level | `WindowLevel_win.cpp` | `WindowLevel_mac.mm` | `WindowLevel_linux.cpp` |
| Platform features | `PlatformFeatures_win.cpp` | `PlatformFeatures_mac.mm` | `PlatformFeatures_linux.cpp` |
| Auto-launch | `AutoLaunchManager_win.cpp` | `AutoLaunchManager_mac.mm` | `AutoLaunchManager_linux.cpp` |
| Install source | `InstallSourceDetector_win.cpp` | `InstallSourceDetector_mac.mm` | `InstallSourceDetector_linux.cpp` |
| PATH utilities | `PathEnvUtils_win.cpp` | N/A | N/A |
| Image color space | N/A | `ImageColorSpaceHelper_mac.mm` | N/A |

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
- `ClickThroughExitButton`
- `PinWindowPlacement`
- `RegionLayoutManager`
- `RegionLayoutRenderer`
- `PinMergeHelper`

Capture history is recorded by `RegionSelector` through `HistoryRecorder` and `HistoryStore` under `src/history/`. Creating a pin window through `PinWindowManager::createPinWindow` does not persist history.

### Recording subsystem

- `QmlRecordingControlBar`
- `QmlCountdownOverlay`
- `RecordingInitTask`
- `RecordingRegionNormalizer`
- `SessionClock`, `FramePacer`, `CapturedFrame` (`src/recording/`): video frame timing
- `CursorTimelineRecorder`, `CursorProbe`, `CursorTimeline` (`src/recording/`): pointer sidecar

Frame timing: every frame carries the monotonic time it was captured (`CapturedFrame::sourceTimeNs`). Backends that capture on their own thread (`deliversFrames()`, X11 today) stamp frames there and emit `frameCaptured`. Polling backends are stamped when `RecordingManager` reads them. `SessionClock` turns capture time into recording time: frames before the start gate (preparation, countdown) and inside pauses are dropped, and so are duplicate or backward times; nanoseconds are rounded to the encoders' milliseconds once. `FramePacer` drops over-delivery from self-capturing backends and counts late frames and stalls; it never adds frames, since the encoders are variable-frame-rate. The window timeline and the shown duration use the same epoch and pause semantics; audio keeps its sample timeline in `TimestampedPcmMixer`. `CapturedFrame::nativeBuffer` holds a platform surface alive (RAII).

Windows backends and GPU frames: Desktop Duplication (`DXGICaptureEngine`) is the default; Windows Graphics Capture (`WGCCaptureEngine`, `deliversFrames()`) is chosen only when `RecordingSettingsManager::windowsGraphicsCapture()` is set (no UI yet) and falls back to Desktop Duplication when it is unsupported or cannot start, with the reason logged. WGC probes `IsCursorCaptureEnabled`, `IsBorderRequired` and `MinUpdateInterval` per property, repeats the last frame while the screen is still, rebuilds its frame pool when the monitor resizes and reports a closed capture item or lost device through `error()` (the recording stops and keeps what it has). Both engines share a multithread-protected `D3D11FrameDevice` with the MP4 encoder (`ICaptureEngine::nativeFrameDevice()` → `IVideoEncoder::setNativeFrameDevice()`). `MediaFoundationEncoder` uses it only after a probe encode on that adapter decodes back and the selected H.264 MFT is D3D11 aware; frames then stay textures (`D3D11Frame`, pooled, returned when the last reference drops) and `D3D11Nv12Converter` turns them into NV12 surfaces (BT.709 limited range, tagged in the stream) without a CPU copy. `RecordingManager` turns native frame output on only for MP4 without a watermark. Within a stream the encoder configuration never changes: CPU frames (watermark, BitBlt fallback) are uploaded and frames from another device are read back, so a failure on the input side keeps the same stream; a lost encoder device rejects the remaining frames. `RecordingSettingsManager::windowsGpuEncoding()` turns the GPU path off.

Pointer sidecar: no default capture backend draws the pointer into frames (`ICaptureEngine::capturesCursor()` is true only for Windows Graphics Capture on systems without `IsCursorCaptureEnabled`, and the sidecar header then records that the pointer is in the video). For a previewed recording, `CursorTimelineRecorder` writes `<video>.cursor.jsonl` instead: one JSON object per line (header, shapes as PNG with hotspot, moves, button presses), in video pixels and media time, appended while recording and flushed every second, so a crash costs at most the torn last line. Positions are polled at 60 Hz and written only on change; shapes are stored once per distinct image and hotspot; button presses arrive as events. Pause and stop release held buttons (marked synthetic) and resume rewrites the current state. The file is capped (64 MiB, about 4.5 hours of constant motion) and lives and dies with the temporary recording like the window timeline. `CursorProbe` reads the pointer passively: on X11 through XFixes (shape) and XInput 2.1+ raw events (clicks, mapped through the user's button order; 2.1 keeps them coming while another application grabs the pointer, so without it clicks are not recorded); elsewhere it falls back to positions from `QCursor::pos()`, and the header says which parts are missing. `SnapTrayCursorInspector` (built with tests) prints a sidecar or replays its events.

Pointer effects: `CursorEffectSettings` (show, size, smoothing, click ripple; serialisable, part of `PreviewEditState` so undo covers it) and `CursorEffectRenderer` draw onto a frame for a source time through a `CursorFrameMapping` (crop, then scale). Rendering is a pure function of the timeline, settings and time: smoothing is a closed-form exponential low-pass over a fixed window, it fades out within `kClickSnapMs` of any button event so clicks stay exact, and ripples are anchored to the recorded press. The same renderer draws the preview (`RecordingCursorOverlay`, over the full frame via `contentRect`) and every export: GIF/WebP in the shared frame loop, MP4 through `VideoTranscodeRequest::frameFilter` on transcoders whose `supportsFrameFilter()` is true (FFmpeg today; it also fills source gaps at the frame rate so effects keep moving over a still picture). Exports take a snapshot of the settings when they start. With no effect drawn the existing move/smart-save fast paths are unchanged.

Zoom: `ZoomTrack` holds sorted, non-overlapping `ZoomSegment`s (time range, scale, focus in source pixels, manual flag) and is part of `PreviewEditState`. `viewportAt(t, bounds)` is the camera: holds during segments, a smootherstep pan between segments closer than two transitions, otherwise an ease out to the whole picture and back; always inside the crop, with its aspect ratio, and never overshooting. `generateAutoZoom()` groups non-synthetic presses and drag paths on visible, on-frame positions by time and distance, frames each group with a margin (capped scale, skipped below a useful zoom), merges groups that would pump, and splits close-in-time groups so each keeps its own clicks zoomed. Exports apply the viewport in the same frame filter before the pointer effects; the preview applies it as a transform on the video and pointer layer (`zoomViewportInView`).

Masks: `RedactionTrack` (source-pixel rects with a time range and Solid/Pixelate/Blur) is part of `PreviewEditState` and is applied first in the frame filter, on the cropped source content, so zoom resampling can never bring masked pixels back. Rects round outward, and a mask covers any frame whose time on screen (up to `kFrameHoldMs`, above the longest export frame interval) overlaps it. The preview burns the same masks into the decoded frame through `VideoPlaybackItem::setFrameProcessor` (ending them a hold later there, since the player position can lead the frame). Masks never fail open: `save()` refuses an export whose format cannot draw them, and the MP4 job refuses any frame filter its transcoder would ignore.

### CLI subsystem

Commands under `include/cli/commands/` include:

- `FullCommand`
- `ScreenCommand`
- `RegionCommand`
- `GuiCommand`
- `CanvasCommand`
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
- Tests are organized by subsystem folders including `Annotations`, `Beautify`, `CLI`, `Cursor`, `Detection`, `Encoding`, `Hotkey`, `IPC`, `PinWindow`, `Qml`, `RecordingManager`, `RegionSelector`, `ScreenCanvas`, `Settings`, `Tools`, `Update`, and `Utils`

### Common patterns to avoid

- Direct `QSettings` access where a settings manager already exists
- Large repetitive `switch` statements where a lookup table is clearer
- Inline magic numbers without named constants

## Related docs

- [Build from Source](build-from-source.md)
- [Release & Packaging](release-packaging.md)
- [CLI](../docs/cli.md)
