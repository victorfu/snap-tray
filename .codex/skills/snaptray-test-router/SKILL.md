---
name: snaptray-test-router
description: Select the smallest sensible build and test plan for a SnapTray change. Use when the user asks which tests to run after editing specific files, wants a minimal verification plan, or wants help narrowing from the full SnapTray test suite to a targeted set. Do not use for initial file routing before implementation; use snaptray-change-router for that. Do not use for architecture critique of the patch itself; use snaptray-architecture-review.
---

# SnapTray test router

Your job is to turn a planned or actual SnapTray change into the smallest reasonable verification plan.

## Verification principles

1. Start with compile confidence, then targeted tests.
2. Bias toward the smallest reasonable subset first.
3. Escalate to broader suites only when a change crosses subsystem boundaries.
4. Avoid recommending the entire test suite unless the change is architectural, build-related, or widely cross-cutting.

## Build guidance

SnapTray already ships with project scripts. Prefer these defaults when the user wants a simple compile check:
- macOS: `./scripts/build.sh`
- Windows: `scripts\build.bat`

For broad validation or when many subsystems changed:
- macOS: `./scripts/run-tests.sh`
- Windows: `scripts\run-tests.bat`

If the user is already inside the build directory or wants a targeted run, prefer `ctest --output-on-failure` with `-R` for the smallest matching test set.

## Test routing rules

### Region capture and selection
If the change touches `src/RegionSelector.cpp`, `include/RegionSelector.h`, or `src/region/*` / `include/region/*`, start with:
- `RegionSelector_SelectionStateManager`
- `RegionSelector_SelectionDirtyRegionPlanner`
- `RegionSelector_RegionInputHandler`
- `RegionSelector_TransientUiCancelGuard` if transient panels, cancel/escape, or floating UI changed
- `RegionSelector_StyleSync` if styles or tool-options syncing changed
- relevant multi-region tests if multi-region behavior changed:
  - `RegionSelector_MultiRegionManagerReorder`
  - `RegionSelector_MultiRegionManagerDeleteReindex`
  - `RegionSelector_MultiRegionReplaceFlow`
  - `RegionSelector_MultiRegionSubToolbar`
  - `RegionSelector_MultiRegionListPanel`

### Pin window
If the change touches `src/PinWindow.cpp`, `src/pinwindow/*`, or pin-specific toolbar/options logic, start with:
- `PinWindow_Transform`
- `PinWindow_Resize`
- `PinWindow_PinWindowPlacement` for geometry/placement
- `PinWindow_StyleSync` for toolbar/options/style propagation
- `PinWindow_CropUndo` for crop/history interactions
- history or merge tests if relevant:
  - `PinWindow_HistoryStore`
  - `PinWindow_HistoryModel`
  - `PinWindow_HistoryRecorder`
  - `PinWindow_PinMergeHelper`

### Screen canvas
If the change touches `src/ScreenCanvasSession.cpp` or canvas toolbar/options logic, start with:
- `ScreenCanvas_StyleSync`
- `ScreenCanvas_Placement` when overlay geometry or toolbar placement changes
- `ScreenCanvas_SessionRecovery` when lifecycle or persistence changes
- `Qml_CanvasToolbarViewModel` if toolbar buttons or states changed

### Tool system
If the change touches `include/tools/*`, `src/tools/*`, or a tool handler, start with:
- `Tools_ToolRegistry`
- the matching tool handler test, for example `Tools_MosaicToolHandler`
- `Qml_PinToolOptionsViewModel` when tool options or option visibility changed
- one host-surface test if the tool appears in a specific host:
  - `RegionSelector_StyleSync`
  - `PinWindow_StyleSync`
  - `ScreenCanvas_StyleSync`

### QML toolbar and options surfaces
If the change touches:
- `src/qml/PinToolOptionsViewModel.cpp`
- `src/qml/PinToolbarViewModel.cpp`
- `src/qml/RegionToolbarViewModel.cpp`
- `src/qml/CanvasToolbarViewModel.cpp`
- `src/qml/toolbar/*`

Start with:
- `Qml_PinToolOptionsViewModel`
- `Qml_CanvasToolbarViewModel` if canvas-specific
- `Qml_FloatingToolbar`
- one or more style sync tests on the affected host surface

### Settings
If the change touches `include/settings/*`, `src/settings/*`, `src/qml/SettingsBackend.cpp`, or `src/qml/settings/*`, start with:
- the matching settings test, for example `Settings_FileSettingsManager`
- `Settings_SettingsBackend` when the backend contract changed
- `Settings_QmlTranslations` for user-visible settings strings or translation-sensitive UI

### Hotkeys
If the change touches `HotkeyManager`, hotkey types, or hotkey settings UI, start with:
- `Hotkey_HotkeyManager`
- `Settings_SettingsBackend` if the settings bridge changed

### Recording, capture, encoding
If the change touches `RecordingManager.cpp`, `src/capture/*`, `src/encoding/*`, or `src/video/*`, start with:
- relevant `RecordingManager_*`
- `Encoding_EncodingWorker` for encode pipeline coordination
- `Encoding_NativeGifEncoder` and `Encoding_EncoderFactory` only when format/encoder selection changed
- `RecordingManager_DXGICaptureEngineThreadLifecycle` for Windows thread/lifecycle changes

### Detection and window detection
If the change touches `src/detection/*`, `src/WindowDetector*`, or `PlatformFeatures::createWindowDetector`, start with:
- relevant `Detection_*`
- `Detection_WindowDetectorQueryMode`
- `Detection_WindowDetectorMacFilters` for macOS-specific filter logic

### Update and packaging
If the change touches `src/update/*`, install-source detection, appcast behavior, or packaging logic, start with:
- `Update_UpdateSettingsManager`
- `Update_UpdateChecker`
- `Update_UpdateCoordinator`
- `Update_InstallSourceDetector`

### CLI and IPC
If the change touches `src/cli/*`, output-path logic, IPC protocol, or single-instance behavior, start with:
- `CLI_*`
- `IPC_*`

### Share
If the change touches `src/share/*`, run:
- `Share_ShareUploadClient`

### Cursor
If the change touches cursor management or cursor QML safety, run:
- `Cursor_*`

## Escalation rules

Escalate to broader verification when:
- the change spans multiple host surfaces, such as tool system plus RegionSelector plus PinWindow
- the change modifies shared enums, shared view-model contracts, or common utility helpers
- the change alters build system, packaging, or platform abstraction boundaries
- the change affects persistent settings used across multiple surfaces

## Required output format

Use this structure:

### Build check
One or two commands.

### Primary tests
The minimum targeted set to run first, with short reasoning.

### Secondary tests
Extra tests only if the primary set passes or if the changed behavior spans more than one surface.

### Why not the full suite yet
One short paragraph.

### Escalate to full suite if
Concrete trigger conditions.

## Important behavior

- Prefer exact test executable names over loose categories.
- Mention when a recommended test is likely slower or more integration-heavy.
- If the changed files do not clearly map to a test, say so and recommend a conservative fallback.
- Never recommend all 105 test executables by default.
