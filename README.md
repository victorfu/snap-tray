# SnapTray - System Tray Screenshot & Recording Tool

**English** | [繁體中文](README.zh-TW.md)

SnapTray is a lightweight tray utility for region screenshots, on-screen annotations, and quick screen recordings. Press F2 (default) to capture a region, or Ctrl+F2 for Screen Canvas.

## Features

- **System Tray Menu**: `Region Capture` (shows current hotkey), `Screen Canvas` (shows current hotkey), `Record Full Screen`, `Close All Pins`, `Exit Click-through`, `Settings`, `Exit`
- **Global Hotkeys**: Customizable in settings with live hotkey registration
  - Region Capture: Default `F2`
  - Screen Canvas: Default `Ctrl+F2`
- **Region Capture Overlay**:
  - Crosshair + magnifier (pixel-level precision)
  - RGB/HEX color preview (Shift to toggle, C to copy color code)
  - Dimension display
  - Selection handles (Snipaste-style)
  - Window detection (macOS/Windows): Auto-detect window under cursor, single-click to select
- **Capture Toolbar**:
  - `Selection` tool (adjust selection area)
  - Annotation tools: `Arrow` / `Pencil` / `Marker` / `Shape` (Rectangle/Ellipse, outline/filled) / `Text` / `Mosaic` / `StepBadge` / `Eraser`
  - `Undo` / `Redo`
  - `Pin` to screen (Enter)
  - `Save` to file (Ctrl+S / Cmd+S on macOS)
  - `Copy` to clipboard (Ctrl+C / Cmd+C on macOS)
  - `Cancel` (Esc)
  - `OCR` text recognition (macOS/Windows, supports Traditional Chinese, Simplified Chinese, English)
  - `Auto Blur` auto-detect and blur faces/text in the selection
  - `Record` screen recording (R) for the selected region
  - `Scroll Capture` long page/scrolling capture for extended content
  - Color + line width controls for supported tools
  - Text formatting controls for Text tool (font family/size, bold/italic/underline)
- **Screen Canvas**:
  - Full-screen annotation mode, draw directly on screen
  - Drawing tools: `Pencil` / `Marker` / `Arrow` / `Shape` (Rectangle)
  - Presentation tools: `Laser Pointer` / `Cursor Highlight` (click ripple)
  - Color + line width controls
  - Undo/Redo/Clear support
  - `Esc` to exit
- **Screen Recording**:
  - Start from capture toolbar (`Record` or `R`)
  - Adjustable region with Start/Cancel
  - Floating control bar with Pause/Resume/Stop/Cancel
  - MP4 (H.264) via native encoder (Media Foundation/AVFoundation); GIF via built-in encoder
  - Optional audio capture (microphone/system audio/both; platform dependent)
- **Pin Windows**:
  - Borderless, always on top
  - Drag to move
  - Mouse wheel to zoom (with scale indicator)
  - Ctrl + mouse wheel to adjust opacity (with indicator)
  - Edge drag to resize
  - Rotation/flip via keyboard: `1` rotate CW, `2` rotate CCW, `3` flip horizontal, `4` flip vertical
  - Double-click or Esc to close
  - Context menu: Copy/Save/OCR/Watermark/Click-through/Close
- **Settings Dialog**:
  - General tab: Launch at startup, toolbar style (Dark/Light), pin window opacity/zoom settings
  - Hotkeys tab: Separate hotkeys for Region Capture and Screen Canvas
  - Watermark tab: Image watermark, opacity, position, and scale
  - Recording tab: Frame rate, output format, quality, countdown, click highlight, audio (enable/source/device)
  - Files tab: Screenshot/recording save paths, filename format
  - About tab: Version information
  - Settings stored via QSettings

## Tech Stack

- **Language**: C++17
- **Framework**: Qt 6 (Widgets/Gui/Svg)
- **Build System**: CMake 3.16+
- **Dependencies**: [QHotkey](https://github.com/Skycoder42/QHotkey) (auto-fetched via FetchContent)
- **macOS Frameworks**:
  - CoreGraphics / ApplicationServices (window detection)
  - AppKit (system integration)
  - Vision (OCR)
  - AVFoundation (MP4 encoding)
  - ScreenCaptureKit (recording, macOS 12.3+)
  - CoreMedia / CoreVideo (recording pipeline)
  - ServiceManagement (auto-launch)
- **Windows APIs**:
  - Desktop Duplication (DXGI/D3D11) for recording
  - Media Foundation (MP4 encoding)
  - WASAPI (audio capture)
  - Windows.Media.Ocr (WinRT OCR)

## System Requirements

SnapTray currently supports macOS and Windows only.

### macOS
- macOS 10.15+ (ScreenCaptureKit recording uses 12.3+ when available)
- Qt 6 (recommend installing via Homebrew)
- Xcode Command Line Tools
- CMake 3.16+
- Git (for FetchContent to fetch QHotkey)

### Windows
- Windows 10+
- Qt 6
- Visual Studio 2019+ or MinGW
- CMake 3.16+
- Git (for FetchContent to fetch QHotkey)

## Build & Run

### Debug Build (Development)

Use Debug build during development for better debugging experience.

**macOS:**
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
open build/SnapTray.app
```

**Windows:**
```batch
# Step 1: Configure (replace with your Qt path)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.x/msvc2022_64

# Step 2: Build
cmake --build build --parallel

# Step 3: Deploy Qt dependencies (required to run the executable)
C:\Qt\6.x\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray.exe

# Step 4: Run
build\bin\SnapTray.exe
```

**Note:** Windows development builds require running `windeployqt` to copy Qt runtime DLLs (Qt6Core.dll, qwindows.dll platform plugin, etc.) alongside the executable. This step is automated in the packaging script for release builds.

### Release Build (Production Testing)

Use Release build to test production behavior before packaging.

**macOS:**
```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
# Output: build/SnapTray.app
```

**Windows:**
```batch
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.x/msvc2022_64
cmake --build build --parallel

# Deploy Qt dependencies
C:\Qt\6.x\msvc2022_64\bin\windeployqt.exe --release build\bin\SnapTray.exe

# Output: build\bin\SnapTray.exe
```

**Note:** For distribution, use the packaging scripts (see below) which automate the deployment process.

### Running Tests

```bash
# Build and run all tests
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
cd build && ctest --output-on-failure
```

### Packaging

The packaging scripts automatically perform Release builds, deploy Qt dependencies, and create installers.

**macOS (DMG):**
```bash
# Prerequisites: brew install qt (if not installed)
# Optional: brew install create-dmg (for prettier DMG)

./packaging/macos/package.sh
# Output: dist/SnapTray-<version>-macOS.dmg
```

**Windows (NSIS Installer):**
```batch
REM Prerequisites:
REM   - Qt 6: https://www.qt.io/download-qt-installer
REM   - NSIS: winget install NSIS.NSIS
REM   - Visual Studio Build Tools or Visual Studio

REM Set Qt path (if not default location)
set QT_PATH=C:\Qt\6.x\msvc2022_64

packaging\windows\package.bat
REM Output: dist\SnapTray-<version>-Setup.exe
```

#### Code Signing (Optional)

Unsigned installers will show warnings during installation:
- macOS: "Cannot verify developer" (requires right-click -> Open)
- Windows: SmartScreen warning

**macOS Signing & Notarization:**
```bash
# Requires Apple Developer Program membership ($99 USD/year)
export CODESIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
export NOTARIZE_APPLE_ID="your@email.com"
export NOTARIZE_TEAM_ID="YOURTEAMID"
export NOTARIZE_PASSWORD="xxxx-xxxx-xxxx-xxxx"  # App-Specific Password
./packaging/macos/package.sh
```

**Windows Signing:**
```batch
REM Requires Code Signing Certificate (purchase from DigiCert, Sectigo, etc.)
set CODESIGN_CERT=path\to\certificate.pfx
set CODESIGN_PASSWORD=your-password
packaging\windows\package.bat
```

## Build Optimization

The build system supports compiler caching for faster incremental builds.

### Local Development

Install a compiler cache tool for significantly faster rebuilds:

**macOS:**
```bash
brew install ccache
```

**Windows:**
```bash
scoop install sccache
```

CMake automatically detects and uses the compiler cache when available. No additional configuration needed.

### Performance Improvements

| Optimization | Location | Effect |
|--------------|----------|--------|
| ccache/sccache auto-detection | CMakeLists.txt | 50-90% faster incremental builds |
| CI ccache (macOS) | ci.yml | 30-50% faster CI |
| CI sccache (Windows) | ci.yml | 30-50% faster CI |

**Note:** Precompiled Headers (PCH) and Unity Build are not used due to incompatibility with Objective-C++ files on macOS.

## Usage

1. After launch, a green square icon appears in the system tray.
2. Press the region capture hotkey (default `F2`) to enter capture mode; or press the screen canvas hotkey (default `Ctrl+F2`) for Screen Canvas mode.
3. **Capture Mode**:
   - Drag mouse to select a region
   - Single-click to quickly select a detected window (macOS/Windows)
   - `Shift`: Toggle color format in magnifier (HEX/RGB)
   - `C`: Copy color code under cursor (before releasing mouse / completing selection)
   - Toolbar appears after releasing mouse
4. **Toolbar Actions**:
   - `Enter` or `Pin`: Pin screenshot as floating window
   - `Ctrl+C` (Windows) / `Cmd+C` (macOS) or `Copy`: Copy to clipboard
   - `Ctrl+S` (Windows) / `Cmd+S` (macOS) or `Save`: Save to file
   - `R` or `Record`: Start screen recording (adjust region, then press Start Recording / Enter)
   - `OCR` (macOS/Windows): Recognize text in selection and copy to clipboard
   - `Undo/Redo`: `Ctrl+Z` / `Ctrl+Shift+Z` (macOS: `Cmd+Z` / `Cmd+Shift+Z`)
   - Annotations: Select a tool and drag within selection area
     - `Text`: Click to enter text
     - `StepBadge`: Click to place auto-numbered step markers
     - `Mosaic`: Drag brush to apply mosaic effect
     - `Eraser`: Drag to erase annotations
5. **Screen Recording**:
   - Use the control bar to Pause/Resume/Stop/Cancel
6. **Screen Canvas Mode**:
   - Toolbar provides drawing and presentation tools
   - Click color/width controls to adjust
   - `Undo` / `Redo`: Undo/redo annotations
   - `Clear`: Clear all annotations
   - `Esc` or `Exit`: Leave Screen Canvas mode
7. **Pin Window Actions**:
   - Drag to move
   - Mouse wheel to zoom
   - Ctrl + mouse wheel to adjust opacity
   - Edge drag to resize
   - `1` rotate CW, `2` rotate CCW, `3` flip horizontal, `4` flip vertical
   - Context menu (Copy/Save/OCR/Watermark/Close)
   - Double-click or `Esc` to close

## Troubleshooting

### Windows: Application fails to start or shows missing DLL errors

If you see errors like:
- "The code execution cannot proceed because Qt6Core.dll was not found"
- "This application failed to start because no Qt platform plugin could be initialized"

**Solution:** Run windeployqt to deploy Qt dependencies:
```batch
C:\Qt\6.x\msvc2022_64\bin\windeployqt.exe build\SnapTray.exe
```

Replace `C:\Qt\6.x\msvc2022_64` with your actual Qt installation path (should match the CMAKE_PREFIX_PATH you used during configuration).

## macOS Permissions

On first capture or recording, the system will request "Screen Recording" permission: `System Preferences -> Privacy & Security -> Screen Recording` and enable SnapTray. Restart the app if necessary.

For window detection, "Accessibility" permission is required: `System Preferences -> Privacy & Security -> Accessibility` and enable SnapTray.

## Project Structure

```
snap-tray/
|-- CMakeLists.txt
|-- README.md
|-- README.zh-TW.md
|-- cmake/
|   |-- Info.plist.in
|   |-- snaptray.rc.in
|   `-- version.h.in
|-- include/
|   |-- MainApplication.h
|   |-- SettingsDialog.h
|   |-- CaptureManager.h
|   |-- RegionSelector.h
|   |-- ScreenCanvas.h
|   |-- PinWindow.h
|   |-- RecordingManager.h
|   |-- WatermarkRenderer.h
|   |-- encoding/
|   |   |-- EncoderFactory.h
|   |   `-- NativeGifEncoder.h
|   |-- ...
|   `-- capture/
|       |-- ICaptureEngine.h
|       |-- QtCaptureEngine.h
|       |-- SCKCaptureEngine.h
|       `-- DXGICaptureEngine.h
|-- src/
|   |-- main.cpp
|   |-- MainApplication.cpp
|   |-- SettingsDialog.cpp
|   |-- CaptureManager.cpp
|   |-- RegionSelector.cpp
|   |-- ScreenCanvas.cpp
|   |-- PinWindow.cpp
|   |-- RecordingManager.cpp
|   |-- WatermarkRenderer.cpp
|   |-- encoding/
|   |   |-- EncoderFactory.cpp
|   |   `-- NativeGifEncoder.cpp
|   |-- ...
|   |-- capture/
|   |   |-- ICaptureEngine.cpp
|   |   |-- QtCaptureEngine.cpp
|   |   |-- SCKCaptureEngine_mac.mm
|   |   `-- DXGICaptureEngine_win.cpp
|   `-- platform/
|       |-- WindowLevel_mac.mm
|       |-- WindowLevel_win.cpp
|       |-- PlatformFeatures_mac.mm
|       `-- PlatformFeatures_win.cpp
|-- resources/
|   |-- resources.qrc
|   `-- icons/
|       |-- snaptray.svg
|       |-- snaptray.png
|       |-- snaptray.icns
|       `-- snaptray.ico
`-- packaging/
    |-- macos/
    |   |-- package.sh
    |   `-- entitlements.plist
    `-- windows/
        |-- package.bat
        |-- installer.nsi
        `-- license.txt
```

## Architecture

The codebase follows a modular architecture with extracted components for maintainability:

### Extracted Components

| Component | Location | Responsibility |
|-----------|----------|----------------|
| `MagnifierPanel` | `src/region/` | Magnifier rendering and caching |
| `UpdateThrottler` | `src/region/` | Event throttling logic |
| `TextAnnotationEditor` | `src/region/` | Text annotation editing/transformation |
| `SelectionStateManager` | `src/region/` | Selection state and operations |
| `RadiusSliderWidget` | `src/region/` | Radius slider control for tools |
| `AnnotationSettingsManager` | `src/settings/` | Centralized annotation settings |
| `FileSettingsManager` | `src/settings/` | File path settings |
| `PinWindowSettingsManager` | `src/settings/` | Pin window settings |
| `ImageTransformer` | `src/pinwindow/` | Image rotation/flip/scale |
| `ResizeHandler` | `src/pinwindow/` | Window edge resize |
| `UIIndicators` | `src/pinwindow/` | Scale/opacity/click-through indicators |
| `ClickThroughExitButton` | `src/pinwindow/` | Exit button for click-through mode |
| Section classes | `src/ui/sections/` | ColorAndWidthWidget sub-components |

### Test Coverage

| Component | Test Count |
|-----------|------------|
| ColorAndWidthWidget | 113 |
| RegionSelector | 108 |
| RecordingManager | 58 |
| Encoding | 43 |
| Detection | 42 |
| PinWindow | 24 |
| Audio | 23 |
| Scrolling | 22 |
| **Total** | **433** |

## Custom App Icon

To replace the icon, prepare a 1024x1024 PNG file, then run:

**macOS (.icns):**
```bash
cd resources/icons
mkdir snaptray.iconset
sips -z 16 16     snaptray.png --out snaptray.iconset/icon_16x16.png
sips -z 32 32     snaptray.png --out snaptray.iconset/icon_16x16@2x.png
sips -z 32 32     snaptray.png --out snaptray.iconset/icon_32x32.png
sips -z 64 64     snaptray.png --out snaptray.iconset/icon_32x32@2x.png
sips -z 128 128   snaptray.png --out snaptray.iconset/icon_128x128.png
sips -z 256 256   snaptray.png --out snaptray.iconset/icon_128x128@2x.png
sips -z 256 256   snaptray.png --out snaptray.iconset/icon_256x256.png
sips -z 512 512   snaptray.png --out snaptray.iconset/icon_256x256@2x.png
sips -z 512 512   snaptray.png --out snaptray.iconset/icon_512x512.png
sips -z 1024 1024 snaptray.png --out snaptray.iconset/icon_512x512@2x.png
iconutil -c icns snaptray.iconset
rm -rf snaptray.iconset
```

**Windows (.ico):**
```bash
# Requires ImageMagick: brew install imagemagick
magick snaptray.png -define icon:auto-resize=256,128,64,48,32,16 snaptray.ico
```

## Known Limitations

- Multi-monitor support: Capture starts on the monitor where cursor is located, but different monitor DPI/scaling needs more real-device testing.
- Screen recording falls back to the Qt capture engine when native APIs (ScreenCaptureKit/DXGI) are unavailable, which may be slower.
- System audio capture on macOS requires macOS 13+ (Ventura) or a virtual audio device (e.g., BlackHole).
- Window detection and OCR are supported on macOS and Windows.

## License

MIT License
