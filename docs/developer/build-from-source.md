---
last_modified_at: 2026-03-26
layout: docs
title: Build from Source
description: Build SnapTray locally on macOS or Windows with the supported scripts, toolchains, and manual CMake flows.
permalink: /developer/build-from-source/
lang: en
route_key: developer_build_from_source
nav_data: developer_nav
docs_copy_key: developer
---

## Supported platforms

SnapTray currently supports macOS and Windows only.

### Runtime targets

- macOS 14.0+
- Windows 10+

### Development prerequisites

- Qt 6.10.1 with Widgets, Gui, Svg, Concurrent, Network, Quick, QuickControls2, and QuickWidgets
- CMake 3.16+
- Ninja
- Git for FetchContent dependencies
- macOS: Xcode Command Line Tools
- Windows: Visual Studio 2022 Build Tools and Windows SDK

### Auto-fetched dependencies

- [QHotkey](https://github.com/Skycoder42/QHotkey) for global hotkeys
- [OpenCV 4.10.0](https://github.com/opencv/opencv) for face and structure detection
- [libwebp 1.3.2](https://github.com/webmproject/libwebp) for WebP animation encoding
- [ZXing-CPP v2.2.1](https://github.com/zxing-cpp/zxing-cpp) for QR and barcode processing

## Recommended build scripts

Use the repository scripts for day-to-day development. They encode the supported build shape and are the default verification path for agents and contributors.

### macOS

```bash
./scripts/build.sh                  # Debug build
./scripts/build-release.sh          # Release build
./scripts/run-tests.sh              # Build and run tests
./scripts/build-and-run.sh          # Debug build + run app
./scripts/build-and-run-release.sh  # Release build + run app
```

### Windows

```batch
scripts\build.bat                   REM Debug build
scripts\build-release.bat           REM Release build
scripts\run-tests.bat               REM Build and run tests
scripts\build-and-run.bat           REM Debug build + run app
scripts\build-and-run-release.bat   REM Release build + run app
```

`scripts\run-tests.bat` prepends `%QT_PATH%\bin` to `PATH` before `ctest`. If you run Windows debug tests manually, do the same first so `Qt6Testd.dll` and other Qt debug DLLs can be found.

For validation, prefer `build.sh` / `build.bat` and the test scripts. Running the app is useful for manual QA but not required for every change.

## PowerShell with MSVC

If you build on Windows from PowerShell, load the Visual Studio developer shell first:

```powershell
Import-Module "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" -DevCmdArguments '-arch=x64' -SkipAutomaticLocation
```

## Manual CMake flows

Use manual configure/build only when you need custom flags or when debugging the build itself.

### macOS (Debug)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
# Output: build/bin/SnapTray-Debug.app
```

### macOS (Release)

```bash
cmake -S . -B release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build release --parallel
# Output: release/bin/SnapTray.app
```

### Windows (Debug)

```batch
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/msvc2022_64
cmake --build build --parallel
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

### Windows (Release)

```batch
cmake -S . -B release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/msvc2022_64
cmake --build release --parallel
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe --release release\bin\SnapTray.exe
```

## Build outputs and build modes

- `Debug` builds use the display name `SnapTray-Debug` and a debug bundle identifier on macOS
- `Release` builds use the shipping app name `SnapTray`
- MCP support is compiled only in `Debug` builds via `SNAPTRAY_ENABLE_MCP`

## Build optimization

The build system automatically uses compiler caching when available.

### Local cache tools

#### macOS

```bash
brew install ccache
```

#### Windows

```bash
scoop install sccache
```

### Current optimization strategy

| Optimization | Where | Effect |
|---|---|---|
| `ccache` / `sccache` auto-detection | `CMakeLists.txt` | Faster incremental builds |
| `/Z7` debug info for MSVC | `CMakeLists.txt` | Improves cacheability for Windows debug builds |
| FetchContent dependency caching | CI | Reduces repeated third-party rebuild cost |

Precompiled headers and Unity builds are intentionally avoided because they conflict poorly with Objective-C++ compilation on macOS.

## Local verification

Preferred validation sequence:

1. Run the platform build script
2. Run the platform test script
3. Manually open the app only when UI or runtime behavior needs verification

On Windows debug builds, manual `ctest` runs should inherit a `PATH` that starts with `%QT_PATH%\bin`, matching `scripts\run-tests.bat`.

## Build troubleshooting

### macOS: app blocked by Gatekeeper

For official signed and notarized releases, Gatekeeper warnings should not appear. For local ad-hoc builds, you can clear quarantine attributes if needed:

```bash
xattr -cr /Applications/SnapTray.app
```

### Windows: missing DLL or Qt platform plugin

If you see errors such as `Qt6Core.dll was not found` or `no Qt platform plugin could be initialized`, deploy Qt dependencies with `windeployqt`:

```batch
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

Replace the Qt path if your installation lives elsewhere.

## Next step

- Go to [Release & Packaging](release-packaging.md) when you need distributable artifacts
- Go to [Architecture Overview](architecture.md) when you need repository structure or subsystem guidance
