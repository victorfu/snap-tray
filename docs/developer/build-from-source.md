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

- Qt 6.12.0 on Windows/macOS; Qt 6.11.2 on Linux, with Widgets, Gui, Svg, Concurrent, Network, Quick, QuickControls2, and QuickWidgets
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

CI and release packaging use the Qt 6.12.0 `clang_64` SDK with `qtimageformats`.
For local builds, set `QT_ROOT_DIR` to that SDK (for example,
`$HOME/Qt/6.12.0/macos`). Without it, local scripts use `brew --prefix qt`,
which is not version-pinned. When changing SDKs, use a fresh build directory
so existing CMake package caches cannot retain the previous Qt.

```bash
export QT_ROOT_DIR="$HOME/Qt/6.12.0/macos"
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

## Windows capture toolbar timing

Set `SNAPTRAY_TOOLBAR_TIMING=1` before launching SnapTray to trace single-region
selection toolbar latency. This opt-in trace uses `qInfo`, so it works in both
Debug and Release builds. Exit the existing instance first so the new process
receives the environment variable. For a Debug build, run from PowerShell:

```powershell
$env:SNAPTRAY_TOOLBAR_TIMING = '1'
$env:QT_FORCE_STDERR_LOGGING = '1'
& .\build\bin\SnapTray-Debug.exe 2> toolbar-timing.log
```

For Release, use the same environment variables and run:

```powershell
Start-Process .\release\bin\SnapTray.exe -Wait -RedirectStandardError toolbar-timing.log
```

Unset the timing variable after collecting traces. Record the build configuration,
GPU and driver version alongside the log, and compare the first capture after
launch with several subsequent captures.

`ToolbarTiming` records have a selection ID and monotonic `elapsedMs` measured
from the canvas mouse-release handler. Subtract timestamps with the same ID:

| Milestones | What the interval measures |
|---|---|
| `mouseRelease` → `selectionComplete.handoffPending` | Selection completion processing |
| `selectionComplete.handoffPending` → `createView.begin` | Toolbar preparation starts immediately, without waiting for the selection-border paint |
| `createView.begin` → `createView.end` | QQuickView creation, including shared QML engine initialization if needed |
| `setSource.begin` → `setSource.end` | Loading and instantiating the toolbar QML |
| `syncTransientParent.begin` → `syncTransientParent.end` | Native parent/style setup; `nativeSurface.created` marks surface creation |
| `nativeShow.begin` → `nativeShow.end` | Native show call |
| `nativeShow.end` → `show.end` | Window policy, shadow, raise and cursor setup |
| `nativeShow.begin` → `firstBeforeRendering` → `firstAfterRendering` → `firstFrameSwapped` | First render and frame submission milestones |

Render callbacks timestamp directly on the emitting thread; log lines may
interleave with GUI-thread events. `firstFrameSwapped` is a Qt frame-submission
milestone, **not proof that the desktop compositor has presented the pixels**.
Likewise, `chromePaint.callback` is a paint-event callback, not presentation.
Backend-dependent signals may be absent, and scene-graph initialization is absent
when already initialized. The trace logs the Qt graphics API enum when initialized
(Software = 1, Direct3D11 = 4, Direct3D12 = 8).
Attempts cancelled before showing the toolbar can have incomplete traces.
Logging itself adds some overhead; use this to locate large delays rather than
as a precise benchmark. It does not enable prewarming or change display ordering.
The toolbar is positioned and shown synchronously on selection completion.
The detached chrome handoff still gates the control panel, but no longer gates
the toolbar. Its later `chromePaint.callback` and `handoff.queuedCallback` stages
normally fall after the toolbar's GUI-thread timing session has ended and are absent.

### Profiling inside the toolbar QML load

For a backend A/B comparison, the `RegionSelector_StyleSync` test executable
accepts `SNAPTRAY_TEST_TOOLBAR_BACKEND=software` or `default`. Run the
`testSelectionCompletionShowsToolbarBeforeChromePaint` test in a fresh process
for each sample with `SNAPTRAY_TOOLBAR_TIMING=1`, alternate backend order, and
check the logged `graphicsApi` rather than assuming the selected policy's result.
Unset the override afterward. This override applies only to the test executable;
it does not change the application's platform policy. `QSG_INFO=1` can additionally
identify the graphics adapter in a separate diagnostic run.

The `RegionSelector_StyleSync` test supports opt-in QML profiling without enabling
debugging in the application. Build that target, then run the single handoff test
in a fresh process to capture the first toolbar load:

```powershell
$env:PATH = "C:\Qt\6.12.0\msvc2022_64\bin;$env:PATH"
$env:SNAPTRAY_TOOLBAR_TIMING = '1'
$env:SNAPTRAY_PROFILE_TOOLBAR = '1'
Start-Process C:\Qt\6.12.0\msvc2022_64\bin\qmlprofiler.exe -Wait -WindowStyle Hidden -ArgumentList '--include compiling,creating,binding -o build/toolbar-focused.qmltrace build/bin/RegionSelector_StyleSync.exe testSelectionCompletionShowsToolbarBeforeChromePaint -o build/toolbar-focused-test.txt,txt'
Remove-Item Env:SNAPTRAY_PROFILE_TOOLBAR
Remove-Item Env:SNAPTRAY_TOOLBAR_TIMING
```

Open the `.qmltrace` in Qt Creator's QML Profiler. The test enables only profiling
services and explicitly ends recording before process exit because the shared
overlay engine normally survives until process termination. Profiling mode adds
a two-second flush wait; ordinary tests do not enable debugging or wait.
Use the limited feature set above to reduce measurement overhead. Nested event
durations overlap and must not be summed. The profiler's `Compiling` category is
a QML loading/compilation interval, not proof that all that time is spent compiling
source. Compare findings against unprofiled timing runs before drawing conclusions.

## PowerShell with MSVC

The Windows build and test scripts load the x64 MSVC developer environment automatically using Visual Studio Installer's `vswhere`, preserving an existing x64 developer environment. This supplies the standard C++ headers and Windows SDK libraries even when CMake reuses a cached compiler path. Missing headers such as `optional` or `type_traits` usually indicate that this environment was not loaded.

For manual CMake commands from PowerShell, load the Visual Studio developer shell first (adjust the installation path for your machine):

```powershell
Import-Module "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" -DevCmdArguments '-arch=x64' -SkipAutomaticLocation
```

## Manual CMake flows

Use manual configure/build only when you need custom flags or when debugging the build itself.

### macOS (Debug)

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"
cmake --build build --parallel
# Output: build/bin/SnapTray-Debug.app
```

### macOS (Release)

```bash
cmake -S . -B release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$QT_ROOT_DIR"
cmake --build release --parallel
# Output: release/bin/SnapTray.app
```

### Windows (Debug)

```batch
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64
cmake --build build --parallel
C:\Qt\6.12.0\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

### Windows (Release)

```batch
cmake -S . -B release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.12.0/msvc2022_64
cmake --build release --parallel
C:\Qt\6.12.0\msvc2022_64\bin\windeployqt.exe --release release\bin\SnapTray.exe
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
C:\Qt\6.12.0\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

Replace the Qt path if your installation lives elsewhere.

## Next step

- Go to [Release & Packaging](release-packaging.md) when you need distributable artifacts
- Go to [Architecture Overview](architecture.md) when you need repository structure or subsystem guidance
