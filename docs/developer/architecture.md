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

Native frames (macOS): `SCKCaptureEngine::captureNativeFrame()` returns the latest frame with its ScreenCaptureKit `CVPixelBuffer`, and `EncodingWorker` offers that surface to `IVideoEncoder::writeNativeFrame()` before writing the image. `AVFoundationEncoder` appends a BGRA surface of its frame size to the writer as is (no QImage, no new buffer, no copy); anything else is declined and the frame takes the image path, whose copy now goes into the writer's IOSurface buffer pool instead of a new buffer per frame. The image path is also taken when a watermark is drawn (it is composited on the CPU image) and when `RecordingSettingsManager::nativeFrameEncoding()` is off (`recording/nativeFrameEncoding`, on by default; for A/B comparison). `RecordingHealth` counts which path each MP4 frame took and why (`video input:` in the diagnostics). Output is SDR, tagged BT.709; both paths encode the captured pixel values unchanged (the display color tag ScreenCaptureKit attaches is dropped, so the encoder does not convert); HDR is not captured. Writer backpressure drops frames, it never waits.

Windows backends and GPU frames: Desktop Duplication (`DXGICaptureEngine`) is the default; Windows Graphics Capture (`WGCCaptureEngine`, `deliversFrames()`) is chosen only when `RecordingSettingsManager::windowsGraphicsCapture()` is set (no UI yet) and falls back to Desktop Duplication when it is unsupported or cannot start, with the reason logged. WGC probes `IsCursorCaptureEnabled`, `IsBorderRequired` and `MinUpdateInterval` per property, repeats the last frame while the screen is still, rebuilds its frame pool when the monitor resizes and reports a closed capture item or lost device through `error()` (the recording stops and keeps what it has). Both engines share a multithread-protected `D3D11FrameDevice` with the MP4 encoder (`ICaptureEngine::nativeFrameDevice()` → `IVideoEncoder::setNativeFrameDevice()`). `MediaFoundationEncoder` uses it only after a probe encode on that adapter decodes back and the selected H.264 MFT is D3D11 aware; frames then stay textures (`D3D11Frame`, pooled, returned when the last reference drops) and `D3D11Nv12Converter` turns them into NV12 surfaces (BT.709 limited range, tagged in the stream) without a CPU copy. `RecordingManager` turns native frame output on only for MP4 without a watermark. Within a stream the encoder configuration never changes: CPU frames (watermark, BitBlt fallback) are uploaded and frames from another device are read back, so a failure on the input side keeps the same stream; a lost encoder device rejects the remaining frames. `RecordingSettingsManager::nativeFrameEncoding()` off turns the GPU path off as on macOS; image-less GPU frames that the encoder does not take are counted as rejected, never encoded without their watermark.

Pointer sidecar: no default capture backend draws the pointer into frames (`ICaptureEngine::capturesCursor()` is true only for Windows Graphics Capture on systems without `IsCursorCaptureEnabled`, and the sidecar header then records that the pointer is in the video). For a previewed recording, `CursorTimelineRecorder` writes `<video>.cursor.jsonl` instead: one JSON object per line (header, shapes as PNG with hotspot, moves, button presses), in video pixels and media time, appended while recording and flushed every second, so a crash costs at most the torn last line. Positions are polled at 60 Hz and written only on change; shapes are stored once per distinct image and hotspot; button presses arrive as events. Pause and stop release held buttons (marked synthetic) and resume rewrites the current state. The file is capped (64 MiB, about 4.5 hours of constant motion) and lives and dies with the temporary recording like the window timeline. `CursorProbe` reads the pointer passively: on X11 through XFixes (shape) and XInput 2.1+ raw events (clicks, mapped through the user's button order; 2.1 keeps them coming while another application grabs the pointer, so without it clicks are not recorded); elsewhere it falls back to positions from `QCursor::pos()`, and the header says which parts are missing. `SnapTrayCursorInspector` (built with tests) prints a sidecar or replays its events.

Pointer effects: `CursorEffectSettings` (show, size, smoothing, click ripple; serialisable, part of `PreviewEditState` so undo covers it) and `CursorEffectRenderer` draw onto a frame for a source time through a `CursorFrameMapping` (crop, then scale). Rendering is a pure function of the timeline, settings and time: smoothing is a closed-form exponential low-pass over a fixed window, it fades out within `kClickSnapMs` of any button event so clicks stay exact, and ripples are anchored to the recorded press. #186 adds idle hiding (opacity from the time since the last movement or button event and until the next, both found by bounded searches), motion blur (ghost copies at fixed time offsets before the frame), "still end" (positions frozen `stillEndMs` before the clip end the renderer is given, the trim end) and `hiddenRanges` (source-time spans without pointer or ripples; kept out of the remembered preferences by `preferences()`). The same renderer draws the preview (`RecordingCursorOverlay`, over the full frame via `contentRect`) and every export: GIF/WebP in the shared frame loop, MP4 through `VideoTranscodeRequest::frameFilter` on transcoders whose `supportsFrameFilter()` is true (FFmpeg today; it also fills source gaps at the frame rate so effects keep moving over a still picture). Exports take a snapshot of the settings when they start. With no effect drawn the existing move/smart-save fast paths are unchanged.

Separate audio tracks (#189): with both sources and a preview, `TimestampedPcmMixer` (`Config::splitSources`) also hands out each source alone on the mixed chunk's frames; engines that support it (Pulse today) emit `IAudioCaptureEngine::sourceAudioReady`, and `RecordingManager::startAudioTracks` writes them with `AudioTrackWriter` to `<video>.mic.pcm` / `.system.pcm` (16-byte header, PCM16 at frame offsets, explicit zero fill, flushed about every second), removed wherever the other sidecars are. The MP4 keeps the mixed track, so old recordings and the unedited move are unchanged. `AudioMixSettings` (gains, mutes, microphone noise reduction: 80 Hz high-pass plus a soft gate) lives in `PreviewEditState`; `AudioTrackMixer` mixes the tracks for the preview (`IVideoPlayer::setAudioSource`, FFmpeg player) and for exports (`VideoTranscodeRequest::audioTracksVideoPath`, FFmpeg's rebuilt-audio path shared with #185 segments).

Editable projects (#191): `RecordingProject.h` holds `RecordingRecipe` (the `PreviewEditState` plus export choices, versioned, `migrate()` for older versions, unknown keys ignored) and `RecordingProjects` (a folder per project under AppLocalData/RecordingProjects: `recording.<ext>`, its sidecars, `project.json`, `thumbnail.png`; `create/load/update/list/remove/removeOlderThan/contains`). Off by default (`RecordingSettingsManager::keepProjects`). With it on, `RecordingPreviewBackend::completeSave` moves the source and sidecars into a new project instead of deleting them; an unedited save is handed a copy, because the save dialog moves what it gets and drops its sidecars. `setProject()` restores the recipe and starts undo from it; in project mode saving updates the recipe and never deletes the source, and `MainApplication::onPreviewDiscardRequested` skips anything `RecordingProjects::contains`. `RecordingManager::openProjectRecording` puts the manager into Previewing without a session, so crash recovery (temp folder, session manifests) and projects never touch each other's files. The tray's Recording Library (`RecordingLibraryViewModel`, `RecordingLibraryDialog.qml`) opens, deletes and reveals projects; old ones are pruned at startup and after each save.

Quick and edit mode (#193): the mode is `RecordingSettingsManager::showPreview` (edit = on), switched in Settings > Recording and in the recording setup (`ScreenPickerViewModel::editMode`, `SettingsBackend::refreshRecordingMode` re-reads it when the kept settings window is shown again). Quick mode is the unchanged preview-off path: the output format directly, no sidecars, the save dialog right after stopping; it then emits `RecordingManager::quickRecordingSaved`, and `QuickSaveAction` runs `RecordingSettingsManager::quickSaveAction` (copy the file to the clipboard, or show its folder). `RecordingManager::recordingOpensPreview()` is the mode the current recording started with, shown in the tray status.

Area before recording (#194, opt-in via `RecordingSettingsManager::preselectArea`): `MainApplication::startRecordingOn` shows `QmlRecordingAreaSelector` (`RecordingAreaSelector.qml`, recording-only; Region Selector is untouched) on the chosen screen and passes the area to `RecordingManager::startScreenRecording(screen, area)`. `RecordingArea::clampToScreen` keeps it on that screen and at least 64 video pixels. In edit mode the region stays the full screen and `RecordingArea::videoCrop` turns the area into the preview's initial crop, stored as `RecordingSessionInfo::initialCrop` (manifest `initialCrop`) and applied by `RecordingPreviewBackend::setInitialCrop` for fresh and recovered recordings alike. In quick mode the area is the recorded region. A `QmlRecordingBoundary` frames the recorded region (quick mode, the area grown to even pixels) or the area (edit mode) outside its edge while recording, excluded from capture where supported (its id joins `excludedCaptureWindowIds`); where the capture cannot exclude overlays (`recordingControlsInTray`, X11) edit mode shows no frame. `MainApplication::recordingSetupActive()` (screen picker or area selector) holds the other capture flows.

Overlays (#188): `OverlayTrack` (in `PreviewEditState`) holds text and image `OverlaySegment`s with a source-time range, a fade, and geometry relative to the exported picture (top-left as fractions, text height of the shorter side or image width). Images are decoded once on add (`loadImage`, bounded to 2048 px) and kept in memory (PNG in JSON), so the source file may go away. The same `paint()` draws `RecordingOverlayItem` in the preview and the export filter, after masks, zoom, pointer and key hints and before the #184 frame. The capture-time recording watermark (`RecordingManager::applyToRecording`) is unchanged: moving it to an overlay would drop it from direct (no-preview) recordings.

Key hints (#187): off unless `RecordingSettingsManager::keyRecordingMode` (Off / Shortcuts / AllKeys) is switched on in the setup dialog or Settings. `KeyProbe` reports presses passively (X11: XInput2 raw key events on a private connection, `X11KeyProbe_linux.cpp`; macOS/Windows: none yet, `keyRecordingSupported()` false); `KeyTimelineRecorder` keeps only what `KeyCapturePolicy::shouldRecord` allows (mode, never during secure input, never a modifier alone) into `<video>.keys.jsonl` (`KeyTimeline`: media time, Qt key, modifiers, text only in AllKeys; no window titles), started and stopped with the pointer sidecar and removed everywhere it is. The preview reads it; `KeyOverlaySettings` (in `PreviewEditState`, remembered) and `KeyOverlayRenderer` (groups keys closer than 800 ms, counts repeats, fades, bounded to the shown part like the pointer) draw the same hint in `RecordingKeyOverlay` and in the export filter, on the picture after zoom and pointer, before the #184 frame.

Timeline edits (#185): `TimelineEdit` (split points in source time and, per part, deleted/speed; part of `PreviewEditState`, a footage edit that "Reset Crop and Trim" clears) yields a `TimelineMap` for the trim: the kept parts with their speeds, and the only source↔output time mapping (`sourceToOutput`, `outputToSource`, `nextShownSourceMs`). The preview player skips cut parts and multiplies its rate by the part's speed; `trimmedDuration()` is the mapped output length (#190's estimate follows). Effects stay functions of source time; `CursorEffectRenderer::setTimeline` bounds smoothing, idle detection, ripples and motion blur to the part being shown. Exports: a map that is one part at 1× is sent as a plain trim (`startMs`/`endMs`), anything else as `VideoTranscodeRequest::segments` to transcoders whose `supportsSegments()` is true (FFmpeg: frames placed at output times with the same frame-drop/gap-fill rules, and audio decoded with `AudioReader` and re-encoded, the source's for 1× parts and silence for others, written as the video advances), and GIF/WebP sample frames at output times through `outputToSource`.

Recording appearance (#184): `RecordingAppearance` (background preset/solid/blurred video/image, padding and corner presets as fractions of the picture's shorter side, shadow, border, aspect ratio; part of `PreviewEditState`; JSON in `RecordingSettingsManager::appearance`, first seeded from `BeautifySettingsManager`) maps onto `BeautifySettings` and reuses `BeautifyRenderer::drawBackground/drawShadow/calculateOutputSize/calculateInsetRect`. `RecordingAppearanceRenderer` caches the static background, shadow and corner mask, so a frame is one copy and one masked draw (`compose`, QImage only, worker-thread safe); `paintAround` draws the same geometry for the preview (`RecordingAppearanceOverlay`) with the picture's rounded shape left clear. In the effects frame filter it runs last (masks → zoom → pointer → frame); the blurred background is taken right after the masks. The filter then returns `outputSize(content)` frames: `VideoTranscodeRequest::filterOutputSize` tells the transcoder, the GIF/WebP encoders start at that size, and #190 options scale the framed size. The preview fits the framed canvas by transforming the whole stage, so overlays and pointer mappings are unchanged.

MP4 export options (#190): `Mp4ExportOptions` (size preset or custom width, frame rate, quality, codec; JSON in `RecordingSettingsManager::mp4ExportOptions`) turns a cropped source size and rate into the output size (aspect kept, even, never upscaled), rate (never above the source) and bitrate (`VideoBitrate::forQuality`, ×0.6 for HEVC); `Mp4SizeEstimate` is that bitrate (plus 192 kbit/s AAC) over the exported length. `VideoTranscodeRequest` carries `outputSize`, `frameRate` and `videoCodec` for transcoders whose `supportsOutputOptions()` / `supportsVideoCodec()` say so (FFmpeg: scale after the frame filter, so masks, zoom and pointer are drawn at full size first; frames closer than one output interval are dropped, the last one before a source pause is kept; HEVC through hevc_vaapi/hevc_nvenc/libx265 with an `hvc1` tag). `RecordingPreviewBackend::effectiveMp4Options()` drops what the platform cannot do; `changesStream()` decides whether the options alone turn an unedited save into an export, so options that change nothing keep the move/smart-save fast path.

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
