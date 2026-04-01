# SnapTray Project

SnapTray is a Qt 6 screenshot and recording application for macOS and Windows. It provides region capture, on-screen annotation, pin windows, screen canvas mode, MP4 recording with optional audio capture, and GIF/WebP export workflows.

## Canonical Docs

Use these as the source of truth instead of growing this file into a second README:

- Developer docs index: `docs/developer/index.md`
- Build from source: `docs/developer/build-from-source.md`
- Release and packaging: `docs/developer/release-packaging.md`
- Architecture overview: `docs/developer/architecture.md`
- MCP for debug builds: `docs/developer/mcp-debug.md`
- User docs home: `docs/docs/index.md`

## Build Instructions

### Windows

Use `cmd.exe` or PowerShell with the MSVC developer environment loaded.

```batch
scripts\build.bat                   REM Debug build
scripts\build-release.bat           REM Release build
scripts\run-tests.bat               REM Build and run tests
scripts\build-and-run.bat           REM Debug build + run app
scripts\build-and-run-release.bat   REM Release build + run app
```

### macOS

```bash
./scripts/build.sh                  # Debug build
./scripts/build-release.sh          # Release build
./scripts/run-tests.sh              # Build and run tests
./scripts/build-and-run.sh          # Debug build + run app
./scripts/build-and-run-release.sh  # Release build + run app
```

For routine verification, use the build and test scripts. Running the app is only required for manual UI validation.

## Prerequisites

- Qt 6.10.1
- CMake 3.16+
- Ninja
- macOS: Xcode Command Line Tools
- Windows: Visual Studio 2022 Build Tools and Windows SDK
- Git for FetchContent dependencies

Key auto-fetched dependencies:

- QHotkey
- OpenCV 4.10.0
- libwebp 1.3.2
- ZXing-CPP v2.2.1

## Repository Landmarks

- `include/` and `src/` mirror the main subsystems
- `src/qml/` contains overlays, dialogs, panels, toolbar, settings, and shared controls
- `tests/` contains Qt Test suites organized by subsystem
- `docs/` contains the website, user docs, and developer docs
- `packaging/` contains macOS and Windows distribution scripts

For the detailed repository map and subsystem boundaries, use `docs/developer/architecture.md`.

## Architecture Rules

### Settings managers are the preferred configuration boundary

Use the subsystem manager instead of direct `QSettings` access whenever one exists.

Common examples:

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

### Hotkeys are centralized

Use `HotkeyManager` as the entry point for global hotkey registration. Do not create `QHotkey` instances directly inside feature code.

### Tool behavior is data-driven

Use `ToolId`, `ToolDefinition`, handlers, and `ToolRegistry`. Prefer lookup tables and capability maps over repetitive `switch` statements.

### Platform abstraction is explicit

Use `PlatformFeatures` and the platform layer instead of scattering OS-specific checks through feature code.

### Shared glass UI stays shared

Use `GlassRenderer` and existing toolbar style helpers for floating panels instead of inventing new local visual systems.

## Verification Expectations

- Run build, test, and relevant checks before claiming completion
- Prefer `scripts/build.sh` or `scripts/build.bat` for compile verification
- Prefer `scripts/run-tests.sh` or `scripts/run-tests.bat` after substantive changes
- On Windows, if you invoke `ctest` or a Qt test binary directly, prepend `%QT_PATH%\bin` to `PATH` first so `Qt6Testd.dll` and other debug Qt DLLs resolve; `scripts/run-tests.bat` is the canonical example
- If a change touches packaging, signing, or release behavior, verify against `docs/developer/release-packaging.md`
- If a change touches MCP, remember it is a debug-build-only feature

## Release Workflow

- `CHANGELOG.md` is the source of truth for curated release notes. Add or update the matching version section before preparing or pushing a release tag.
- Keep `project(SnapTray VERSION ...)` in `CMakeLists.txt` in sync with the tag name `vX.Y.Z`. The release workflow fails if the tag version and CMake version do not match.
- For release tags, create the local tag with `git tag vX.Y.Z`. Leave pushing the tag to manual execution.
- GitHub Actions handles the rest: macOS and Windows builds, signing/notarization, GitHub Release creation, appcast updates, and website release page generation.
- Do not add Xcode or `MARKETING_VERSION` guidance. SnapTray release versioning is CMake-driven.
- For the full release procedure, use `docs/developer/release-packaging.md`.

## Logging and Style

### Logging

- `qDebug()`: development diagnostics, suppressed in release
- `qWarning()`: hardware or API failures
- `qCritical()`: critical errors

### Style

- Use C++17 features where they improve clarity
- Prefer `auto` for complex types and explicit types for simple primitives
- Use `nullptr` instead of `NULL`
- Prefer `QPoint` and `QRect` over loose coordinate tuples

## Testing Guidance

- Tests live under `tests/<Component>/tst_<Name>.cpp`
- The suite uses Qt Test with helpers such as `QCOMPARE` and `QVERIFY`
- Important test areas include CLI, Cursor, Detection, Encoding, Hotkey, MCP, PinWindow, Qml, RecordingManager, RegionSelector, ScreenCanvas, Settings, Share, Tools, Update, and Utils

## Patterns to Avoid

- Direct `QSettings` access where a settings manager already exists
- Repetitive `switch` statements that should be lookup tables
- Inline magic numbers without named constants

## When You Need More Detail

Do not re-expand this file with long project inventories unless the information is agent-critical and cannot live elsewhere.

- Build and toolchain detail: `docs/developer/build-from-source.md`
- Packaging and signing: `docs/developer/release-packaging.md`
- Repository structure and subsystem ownership: `docs/developer/architecture.md`
- Debug-only MCP behavior: `docs/developer/mcp-debug.md`
