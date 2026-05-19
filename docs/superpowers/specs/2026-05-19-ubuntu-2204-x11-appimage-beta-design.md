# Ubuntu 22.04 X11 AppImage Beta Design

## Context

SnapTray currently supports macOS and Windows. The project already has a platform
library and platform feature boundary, but most native implementations are only
present for macOS and Windows. Ubuntu support therefore needs a deliberate Linux
platform slice rather than accidental compilation through generic Qt paths.

The first Linux target is a user-facing beta for Ubuntu 22.04 on X11. Wayland is
out of scope for this beta. Recording and OCR are also out of scope and must not
be shown in the Linux UI.

## Goals

- Support Ubuntu 22.04 X11 as a beta runtime target.
- Produce an AppImage artifact as the only Linux beta distribution format.
- Keep region capture, annotation, copy/save, pin windows, screen canvas, tray
  menu, settings, CLI basics, and global hotkeys usable on X11.
- Hide recording and OCR from Linux UI surfaces.
- Keep Linux platform decisions centralized behind existing platform boundaries.
- Add CI/build/package documentation so Linux support is reproducible.

## Non-Goals

- Wayland support.
- Linux MP4 recording, audio capture, recording preview, or recording settings.
- Linux OCR.
- Linux in-app updates.
- Native Linux window detection in the first beta.
- `.deb`, Flatpak, Snap, or package-manager distribution.

## Recommended Approach

Use a progressive Linux beta slice:

1. Add Linux platform source sets and Linux implementations or stubs for missing
   platform symbols.
2. Use the existing Qt capture fallback on X11 as the first screenshot engine.
3. Keep global hotkeys routed through `HotkeyManager` and QHotkey.
4. Gate feature visibility through a centralized platform capability model.
5. Package with AppImage and verify with Ubuntu 22.04 CI.

This keeps scope aligned with the beta target while preserving room for later
Wayland, PipeWire, portal, or native X11 work.

## Architecture

The Linux beta adds explicit `Q_OS_LINUX` handling in CMake and platform
implementation files. It must not compile macOS Objective-C++ files or Windows
native files on Linux.

Expected Linux implementation files include:

- `src/platform/PlatformFeatures_linux.cpp`
- `src/platform/WindowLevel_linux.cpp`
- `src/AutoLaunchManager_linux.cpp`
- `src/update/InstallSourceDetector_linux.cpp`
- `src/update/UpdateServiceFactory_linux.cpp`
- Linux-safe QML window or overlay `.cpp` implementations where current source
  lists include `.mm` files unconditionally.

Linux implementations should prefer no-op behavior only when the feature is
truly optional. Required user-facing beta features, such as tray startup,
capture, and global hotkeys, must be functional or report a clear error.

## Platform Capabilities

Add or extend a centralized capability model, backed by `PlatformFeatures`, so
UI, CLI, settings, and tests use the same feature decisions.

Linux beta capability values:

- `supportsRecording = false`
- `supportsOCR = false`
- `supportsGlobalHotkeys = true`
- `supportsWindowDetection = false`
- `supportsInAppUpdates = false`
- `supportedDisplayServer = X11`

Global hotkey availability still depends on runtime registration. QHotkey
registration results remain the source of truth for individual hotkey status.

## User-Facing Behavior

### Supported On Linux X11

- Tray menu and tray-first app startup.
- Region capture with manual selection.
- Annotation tools.
- Copy and save flows.
- Pin windows.
- Screen canvas.
- Settings surfaces relevant to supported features.
- Global hotkeys, including default hotkeys and editable hotkey settings.
- CLI commands for supported screenshot/canvas/pin/config flows.

### Hidden On Linux

- Recording tray menu entries.
- Recording settings page.
- Recording QML entry points.
- OCR settings page.
- OCR tools and OCR result dialogs.
- CLI help entries for unsupported recording or OCR commands.

If a user directly invokes an unsupported command path, the app must return a
non-zero result with a clear Linux beta unsupported message instead of entering a
partial workflow.

## Data Flow

1. Startup reads platform capabilities.
2. GUI startup checks for an X11 session before presenting Linux beta UI.
3. `MainApplication` builds tray actions from capabilities.
4. Settings backends and QML view models hide pages or controls based on the
   same capabilities.
5. CLI help and command dispatch use the same capabilities.
6. `CaptureManager` receives no `WindowDetector` on Linux and continues with
   manual region selection.
7. `HotkeyManager` registers enabled hotkeys through QHotkey and reports status
   through the existing hotkey status model.

## Error Handling

- Non-X11 Linux GUI session: show a clear "Ubuntu 22.04 beta supports X11
  sessions only" message and exit.
- Non-X11 Linux CLI session: return a non-zero exit code with the same support
  boundary.
- Global hotkey registration failure: keep the app running and show failed
  status through existing hotkey UI.
- Missing or unavailable system tray: report a startup error and exit, because
  SnapTray is tray-first.
- Screenshot failure: use the existing capture error path and document X11
  troubleshooting.
- Missing AppImage runtime assets or Qt plugins: fail packaging or smoke tests
  before publishing.

## Build And Packaging

Add Linux build and packaging scripts:

- `scripts/build-linux.sh` or extend `scripts/build.sh` with Linux-specific
  configuration.
- `scripts/run-tests-linux.sh` or extend `scripts/run-tests.sh`.
- `packaging/linux/package-appimage.sh`.

The AppImage output should be named:

```text
dist/SnapTray-<version>-x86_64.AppImage
```

The AppImage package should include:

- SnapTray executable.
- Required Qt runtime libraries and plugins.
- QML imports used by the app.
- `.desktop` metadata.
- Application icon.
- AppImage metadata required by the chosen packaging tool.

The default packaging toolchain is `linuxdeploy` plus the Qt plugin and
`appimagetool`. If that toolchain cannot deploy this Qt 6.10.1 application
reliably in CI, the implementation plan may switch to `linuxdeployqt`, but the
artifact format and user-facing behavior remain unchanged.

The first beta does not include Linux update feeds or in-app update integration.

## CI

Add an Ubuntu 22.04 CI job that:

- Installs Qt 6.10.1 and required build tooling.
- Configures CMake for Release.
- Builds the app and tests.
- Runs unit tests.

Add an AppImage packaging job for release builds or release workflow expansion.
The package job should run at least a smoke check that verifies the AppImage can
start far enough to print version/help output without missing shared libraries
or Qt plugins.

## Tests

Add or update tests for:

- Linux capability values.
- Recording and OCR hidden in Linux menus/settings models.
- CLI help excluding unsupported Linux commands.
- Unsupported direct command invocation returning non-zero.
- Update service selection returning unsupported or external-managed behavior on
  Linux.
- Hotkey status flow remaining visible when registration fails.

Manual Ubuntu 22.04 X11 QA must cover:

- AppImage launch.
- Tray icon and tray menu.
- Default hotkey triggering region capture.
- Hotkey editing and status display.
- Region capture, annotation, copy, save.
- Pin window creation.
- Screen canvas.
- Recording and OCR absent from UI.
- Wayland session rejection message.

## Documentation

Update developer and user docs:

- Supported platforms: add Ubuntu 22.04 X11 beta.
- Build from source: add Linux prerequisites and commands.
- Release and packaging: add AppImage workflow.
- User docs/troubleshooting: document X11-only beta limitation and AppImage use.
- Architecture overview: add Linux platform map and capability notes.

Do not expand `AGENTS.md`; canonical docs remain under `docs/developer/` and
`docs/docs/`.

## Decisions

No open product decisions remain for the beta scope.
