---
last_modified_at: 2026-03-26
layout: docs
title: Build from Source
seo_title: "Build SnapTray from Source: macOS, Windows and Linux Beta Toolchain"
description: Build SnapTray locally on macOS, Windows, or Ubuntu 22.04 X11 beta with the supported scripts, toolchains, and manual CMake flows.
permalink: /developer/build-from-source/
lang: en
route_key: developer_build_from_source
nav_data: developer_nav
docs_copy_key: developer
---

## Supported platforms

SnapTray currently supports macOS, Windows, and Ubuntu 22.04 X11 beta.

### Runtime targets

- macOS 14.0+
- Windows 10+
  - Live Update requires Windows 10 version 2004 (build 19041) or later.
- Ubuntu 22.04 X11 beta

### Development prerequisites

- Qt 6.11.2 with Widgets, Gui, Svg, Concurrent, Network, Quick, QuickControls2, and QuickWidgets
- Qt Image Formats add-on (`qtimageformats`) for WebP/TIFF image import
- CMake 3.16+
- Ninja
- Git for FetchContent dependencies
- macOS: Xcode Command Line Tools
- Windows: Visual Studio 2022 Build Tools and Windows SDK

### Linux

- Ubuntu 22.04 X11 session
- Qt 6.11.2 with Widgets, Gui, Svg, Concurrent, Network, Quick, QuickControls2, and QuickWidgets
- Qt Image Formats add-on (`qtimageformats`) for WebP/TIFF image import
- CMake 3.16+
- Ninja
- Git
- X11 development libraries required by Qt and QHotkey

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

### Linux

Ubuntu 22.04 X11 beta uses the same shell scripts as macOS:

```bash
./scripts/build.sh
./scripts/run-tests.sh
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

The Windows scripts reconfigure an existing build directory when its cached Qt installation differs from `QT_PATH` or its build type differs from the script's Debug/Release mode. They preserve unrelated CMake options, refresh cached Qt package paths, and redeploy the selected Qt runtime before running the app or tests. Failed configuration, builds, or deployment stop the script; the next run retries the pending update.

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
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64
cmake --build build --parallel
C:\Qt\6.11.2\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

### Windows (Release)

```batch
cmake -S . -B release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64
cmake --build release --parallel
C:\Qt\6.11.2\msvc2022_64\bin\windeployqt.exe --release release\bin\SnapTray.exe
```

## Build outputs and build modes

- `Debug` builds use the display name `SnapTray-Debug` and a debug bundle identifier on macOS
- `Release` builds use the shipping app name `SnapTray`

### Experimental Linux recording with system FFmpeg

The optional development prototype directly links the host's FFmpeg libraries;
it does not launch the `ffmpeg` executable or download/bundle an FFmpeg build.
The normal Linux app keeps recording hidden and unsupported even when this
option is enabled. Only the standalone prototype and its tests link FFmpeg.
Windows and macOS continue to use their existing encoders.

On Ubuntu 22.04, install the development packages and enable the option:

```bash
sudo apt install pkg-config libavcodec-dev libavformat-dev libavutil-dev libswscale-dev
cmake -S . -B build -DSNAPTRAY_ENABLE_FFMPEG_PROTOTYPE=ON
./scripts/build.sh
# Standalone diagnostics window:
cmake --build build --target SnapTrayRecordingPrototype
./build/bin/SnapTrayRecordingPrototype
./scripts/run-tests.sh
```

In the standalone diagnostics window, select a screen and recording duration,
then choose **Record screen** and an output path. This window records the selected X11
screen at a target 30 fps to silent H.264 MP4. Stop early with **Stop and save**.
Closing the window cancels the recording. An existing destination is replaced
only after successful finalization. The prototype window itself is captured if
it is on the selected screen; capture exclusion is not implemented on X11.
Even physical screen dimensions are required for YUV420P. There is no preview,
audio, cropping, or hardware-encoder selection in this prototype.

The host FFmpeg must expose `libx264` or `libopenh264`; a decoder alone is not
sufficient. `libx264` is preferred when both are present. Frames are captured on
the GUI thread and encoded using the existing bounded `EncodingWorker` queue.
The completion message reports encoded frames, queue rejections, and elapsed
capture time. Timer delays can also reduce achieved fps without a queue
rejection. Use these figures alongside a system CPU monitor for manual
1080p/30 fps evaluation; the automated Xvfb test is not a desktop performance
benchmark.

Runtime dependencies must match the linked FFmpeg ABI. A build against Ubuntu
22.04's `libavcodec.so.58` cannot assume another distribution's newer SONAME is
interchangeable. Missing shared libraries prevent prototype startup; missing
H.264 encoders are reported by the prototype. System codec updates do not
require shipping codec binaries with SnapTray, but FFmpeg build options and
licensing still need to be considered when choosing distribution packages.

The standalone prototype is for local development and is not included in the
AppImage release. Distributing it would require an explicit packaging decision
and runtime dependency declarations; deployment tools may automatically bundle
its linked libraries. Disable the option with
`cmake -S . -B build -DSNAPTRAY_ENABLE_FFMPEG_PROTOTYPE=OFF`.

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
C:\Qt\6.11.2\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

Replace the Qt path if your installation lives elsewhere.

## Next step

- Go to [Release & Packaging](release-packaging.md) when you need distributable artifacts
- Go to [Architecture Overview](architecture.md) when you need repository structure or subsystem guidance
