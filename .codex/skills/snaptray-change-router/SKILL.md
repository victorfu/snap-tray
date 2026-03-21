---
name: snaptray-change-router
description: Analyze a requested change in the SnapTray repository and route it to the likely modules, files, tests, docs, and platform surfaces. Use when the user asks where to implement a feature, fix, refactor, or bug investigation in SnapTray, or asks how to split work into safe patches. Do not use for final PR review or for selecting the exact post-change test command after the diff already exists; use snaptray-architecture-review or snaptray-test-router instead.
---

# SnapTray change router

Your job is to translate a requested change into the smallest correct set of SnapTray surfaces that should be inspected or modified.

## Primary goals

1. Classify the request into one or more SnapTray subsystems.
2. Name the most likely files and directories to inspect.
3. Call out cross-cutting follow-up surfaces that are easy to forget.
4. Recommend the smallest sensible test set.
5. Suggest a safe patch split when the request is broad.

## SnapTray subsystem map

### Region capture and selection
Use this surface when the task mentions region capture, selection boxes, magnifier, multi-region, export from selection, transient UI, OCR/QR actions in capture mode, or floating capture controls.

Likely files:
- `include/RegionSelector.h`
- `src/RegionSelector.cpp`
- `include/region/*`
- `src/region/*`
- `src/qml/RegionToolbarViewModel.cpp`
- `src/qml/PinToolOptionsViewModel.cpp`
- `src/qml/MultiRegionListViewModel.cpp`
- `src/qml/ShortcutHintsViewModel.cpp`

Primary tests:
- `RegionSelector_*`
- `Qml_FloatingToolbar`
- `Qml_PinToolOptionsViewModel`

### Pin window
Use this surface when the task mentions pinned windows, crop in pin mode, pin placement, merge, pin history, OCR inside pins, or toolbar/options in pin mode.

Likely files:
- `include/PinWindow.h`
- `src/PinWindow.cpp`
- `include/pinwindow/*`
- `src/pinwindow/*`
- `src/qml/PinToolbarViewModel.cpp`
- `src/qml/PinToolOptionsViewModel.cpp`
- `src/qml/PinHistoryBackend.cpp`

Primary tests:
- `PinWindow_*`
- `Qml_PinToolOptionsViewModel`
- `Qml_FloatingToolbar`

### Screen canvas
Use this surface when the task mentions annotating directly on screen, canvas mode, whiteboard/blackboard toggle, canvas toolbar, or canvas session recovery.

Likely files:
- `include/ScreenCanvasSession.h`
- `src/ScreenCanvasSession.cpp`
- `src/qml/CanvasToolbarViewModel.cpp`
- `src/qml/PinToolOptionsViewModel.cpp`
- `src/qml/toolbar/*`

Primary tests:
- `ScreenCanvas_*`
- `Qml_CanvasToolbarViewModel`
- `Qml_PinToolOptionsViewModel`

### Tool system and annotation workflows
Use this surface when the task adds or changes a tool, toolbar button, tool availability, tool options, or tool behavior.

Likely files:
- `include/tools/ToolId.h`
- `include/tools/ToolDefinition.h`
- `include/tools/ToolRegistry.h`
- `include/tools/ToolSectionConfig.h`
- `include/tools/ToolManager.h`
- `include/tools/handlers/*`
- `src/tools/ToolRegistry.cpp`
- `src/tools/ToolSectionConfig.cpp`
- `src/tools/ToolManager.cpp`
- `src/tools/handlers/*`
- `src/qml/PinToolbarViewModel.cpp`
- `src/qml/RegionToolbarViewModel.cpp`
- `src/qml/CanvasToolbarViewModel.cpp`
- `src/qml/PinToolOptionsViewModel.cpp`

Primary tests:
- `Tools_*`
- relevant host tests: `PinWindow_*`, `RegionSelector_*`, or `ScreenCanvas_*`
- relevant QML tests if the toolbar/options surface changes

### Settings and preferences
Use this surface when the task changes persistence, defaults, settings UI, theme or language behavior, or settings-related migration.

Likely files:
- `include/settings/*`
- `src/settings/*`
- `src/qml/SettingsBackend.cpp`
- `src/qml/QmlSettingsWindow.cpp`
- `src/qml/settings/*`
- `include/update/UpdateSettingsManager.h`
- `src/update/UpdateSettingsManager.cpp`

Primary tests:
- matching `Settings_*`
- `Update_UpdateSettingsManager` for update preferences
- `Settings_QmlTranslations` for user-facing text or settings localization changes

### Hotkeys
Use this surface when the task changes global shortcuts, registration policy, conflict handling, or hotkey settings UI.

Likely files:
- `include/hotkey/HotkeyManager.h`
- `src/hotkey/HotkeyManager.cpp`
- `include/hotkey/HotkeyTypes.h`
- `src/qml/settings/HotkeySettings.qml`
- `src/qml/SettingsBackend.cpp`

Primary tests:
- `Hotkey_HotkeyManager`
- `Settings_SettingsBackend` if the UI/backend contract changes

### Recording, capture, encoding, playback
Use this surface when the task mentions recording, screen/audio capture engines, output format, trimming, preview, or threading around recording.

Likely files:
- `include/RecordingManager.h`
- `src/RecordingManager.cpp`
- `src/capture/*`
- `src/encoding/*`
- `src/video/*`
- `src/qml/RecordingPreviewBackend.mm`
- `include/settings/RecordingSettingsManager.h`
- `src/settings/RecordingSettingsManager.cpp`

Primary tests:
- `RecordingManager_*`
- `Encoding_*`
- `Settings_*` if defaults or output paths change

### Platform-specific integration
Use this surface when the request touches OS permissions, update services, packaging, OCR manager creation, window detection, tray icon behavior, install source detection, or platform-specific fallbacks.

Likely files:
- `include/PlatformFeatures.h`
- `src/platform/*`
- `src/update/*`
- `packaging/macos/*`
- `packaging/windows/*`
- `docs/updates/macos.xml`
- `docs/updates/windows.xml`
- `src/WindowDetector.mm`
- `src/WindowDetector_win.cpp`
- `src/WindowDetectorMacFilters.h`

Primary tests:
- `Update_*`
- `Detection_WindowDetector*`
- `RecordingManager_DXGICaptureEngineThreadLifecycle` when Windows capture threading changes
- `Encoding_*` when encoder behavior changes

### CLI and IPC
Use this surface when the task mentions command-line behavior, output-path handling, numeric validation, or single-instance behavior.

Likely files:
- `include/cli/*`
- `src/cli/*`
- `include/IPCProtocol.h`
- `src/IPCProtocol.cpp`
- `src/SingleInstanceGuard.cpp`

Primary tests:
- `CLI_*`
- `IPC_*`

### Detection, OCR, QR, auto blur
Use this surface when the task changes face detection, table detection, credentials detection, OCR region behavior, or QR code flows.

Likely files:
- `include/detection/*`
- `src/detection/*`
- `src/QRCodeResultViewModel.cpp`
- OCR-related creation paths via `PlatformFeatures`
- selection/pin integrations that invoke OCR or QR

Primary tests:
- `Detection_*`
- relevant `RegionSelector_*` or `PinWindow_*` tests if the result UI changes

### Share and upload
Use this surface when the task touches quick share, upload result views, password/share result flows, or share config.

Likely files:
- `include/share/*`
- `src/share/*`
- `src/qml/SharePasswordViewModel.cpp`
- `src/qml/ShareResultViewModel.cpp`
- quick share docs in `docs/docs/tutorials/quick-share.md`

Primary tests:
- `Share_ShareUploadClient`
- relevant settings tests if share configuration becomes user-configurable

### MCP debug surface
Use this only when the task is clearly about the built-in MCP server or developer/debug workflows.

Likely files:
- `include/mcp/*`
- `src/mcp/*`

Primary tests:
- `MCP_*`

Guardrail: treat MCP as debug-only unless the task explicitly says otherwise.

## Cross-cutting follow-up checks

For every routed change, explicitly consider whether it also needs:
- a settings manager update
- `SettingsBackend` or QML settings UI updates
- toolbar or tool-options view model updates
- docs in both `docs/docs/*` and `docs/zh-tw/docs/*`
- platform symmetry between macOS and Windows
- a release/update follow-up if packaging, updater, or install source changes

## Required output format

Use this exact structure:

### Request summary
One short paragraph.

### Primary subsystem(s)
Bullet list.

### Likely files to inspect first
Ranked list with one-line reasoning per file or directory.

### Easy-to-miss follow-up surfaces
Only include realistic follow-up surfaces.

### Recommended tests
Split into `primary` and `secondary`.

### Suggested patch split
Use 1 to 4 patches. Keep each patch behaviorally coherent.

### Uncertainties
List only real ambiguities.

## Important behavior

- Prefer naming concrete files over vague directories when confidence is high.
- Keep the plan minimal; do not spray the whole repo.
- If the request is broad, propose a safe patch split before implementation.
- If the request obviously belongs to one of the more specialized SnapTray skills, say so and recommend that skill next.
