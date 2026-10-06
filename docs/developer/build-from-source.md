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
- FFmpeg, PulseAudio, and XCB shared-memory development packages (see Linux recording below)

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

### Linux recording with system FFmpeg

Linux X11 builds enable `SNAPTRAY_ENABLE_LINUX_RECORDING` by default. The backend
links the distribution's FFmpeg and PulseAudio libraries directly; it does not
launch the `ffmpeg` executable. Windows and macOS retain their native backends.
Existing `SNAPTRAY_ENABLE_FFMPEG_PROTOTYPE` build caches migrate to the new option.

```bash
sudo apt install pkg-config libavcodec-dev libavformat-dev libavutil-dev \
  libswscale-dev libswresample-dev libpulse-dev libxcb-shm0-dev libxcb-randr0-dev
./scripts/build-and-run.sh
```

Select **Record Screen** in the tray and choose a screen. Recording uses a
background X11 connection with MIT-SHM when available, falling back to XCB image
transfers. Completed frames go directly to the encoding queue, independently of
GUI event delivery. The floating preparation/countdown UI is hidden before recording;
use the tray's **Pause Recording**, **Resume Recording**, and **Stop Recording**
actions or the recording hotkey. X11 captures the composed desktop, so menus and
other visible applications can still appear. Live pin updates remain disabled.

Microphone and system audio use PulseAudio, including PipeWire's PulseAudio
compatibility server. System audio is the default output device's monitor;
**Both** mixes that monitor with the selected microphone at 48 kHz stereo.
Recordings encode H.264 video and optional AAC audio. Recording Preview supports
playback with audio, seeking, cropping, trimming, MP4 export with AAC passthrough,
and GIF/WebP conversion through the FFmpeg frame reader.

The encoder attempts VA-API on available render nodes, then NVIDIA NVENC, then
software H.264 (`libx264` or `libopenh264`). A listed encoder is not considered
usable until initialization succeeds. GPU drivers must be supplied by the host.
For diagnosis, `SNAPTRAY_FFMPEG_ENCODER=software` forces CPU encoding;
`h264_vaapi` or `h264_nvenc` selects that hardware attempt with software fallback.
Debug logs report the selected encoder and mean capture/conversion/encoding
costs. Actual frame rate still depends on screen resolution, CPU/GPU, and load.

```bash
# Canonical suite; audio integration tests use an isolated PulseAudio server.
sudo apt install pulseaudio pulseaudio-utils xvfb xauth
./scripts/run-tests.sh
# Generated media only; no screen or microphone access:
QT_QPA_PLATFORM=offscreen ./build/bin/SnapTray --internal-recording-smoke-check
# Optional 4K throughput check, generated scene on an isolated display:
SNAPTRAY_TEST_ISOLATED_X11=1 SNAPTRAY_TEST_BENCHMARK_4K=1 \
  SNAPTRAY_FFMPEG_ENCODER=software QT_QUICK_BACKEND=software XDG_SESSION_TYPE=x11 \
  xvfb-run -a -s '-screen 0 3840x2160x24' \
  ./build/bin/Encoding_LinuxRecordingPrototype benchmark4K
# Optional standalone silent-recording diagnostic window:
cmake --build build --target SnapTrayRecordingPrototype
```

Unpackaged binaries need the FFmpeg ABI used at build time. An Ubuntu 22.04 build
linked to `libavcodec.so.58` cannot substitute another distribution's newer
SONAME. AppImage packaging bundles the build distribution's shared media
libraries as a matching set and verifies encoding, decoding, and crop export
inside the extracted artifact. The host still supplies its audio server and GPU
drivers. Disable recording for a minimal build with
`cmake -S . -B build -DSNAPTRAY_ENABLE_LINUX_RECORDING=OFF`.

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
