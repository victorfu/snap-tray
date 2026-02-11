# SnapTray - System Tray Screenshot & Recording Tool

**English** | [繁體中文](README.zh-TW.md)

SnapTray is a lightweight tray utility for region screenshots, on-screen annotations, and quick screen recordings. Press F2 (default) to capture a region, or Ctrl+F2 for Screen Canvas.

## Features

- **System Tray Menu**: `Region Capture` (shows current hotkey), `Screen Canvas` (shows current hotkey), `Pin from Image...`, `Pin History`, `Close All Pins`, `Record Full Screen`, `Settings`, `Exit`
- **Global Hotkeys**: Customizable in settings with live hotkey registration
  - Region Capture: Default `F2`
  - Screen Canvas: Default `Ctrl+F2`
  - Additional configurable actions: `Paste` (default `F3`), `Quick Pin` (default `Shift+F2`), `Pin from Image`, `Pin History`, `Record Full Screen`
- **Region Capture Overlay**:
  - Crosshair + magnifier (pixel-level precision)
  - RGB/HEX color preview (Shift to toggle, C to copy color code)
  - Dimension display
  - Selection handles (Snipaste-style)
  - Aspect ratio lock (hold Shift to constrain proportions)
  - Multi-region selection (capture multiple areas, merge or save separately)
  - Include cursor option in screenshots
  - Window detection (macOS/Windows): Auto-detect window under cursor, single-click to select
  - Right-click to cancel selection
- **Capture Toolbar**:
  - `Selection` tool (adjust selection area)
  - Annotation tools: `Arrow` / `Pencil` / `Marker` / `Shape` (Rectangle/Ellipse, outline/filled) / `Text` / `Mosaic` / `StepBadge` / `EmojiSticker` / `Eraser`
  - `Undo` / `Redo`
  - `Pin` to screen (Enter)
  - `Save` to file (Ctrl+S / Cmd+S on macOS)
  - `Copy` to clipboard (Ctrl+C / Cmd+C on macOS)
  - `Cancel` (Esc)
  - `OCR` text recognition (macOS/Windows; available languages depend on installed system OCR language packs)
  - `QR Code Scan` (scan QR/barcodes from the selected region)
  - `Auto Blur` auto-detect and blur faces in the selection
  - `Record` screen recording (R) for the selected region
  - `Multi-Region` capture toggle (`M`)
  - Color + line width controls for supported tools
  - Text formatting controls for Text tool (font family/size, bold/italic/underline)
- **Screen Canvas**:
  - Full-screen annotation mode, draw directly on screen
  - Drawing tools: `Pencil` / `Marker` / `Arrow` / `Shape` / `StepBadge` / `Text` / `EmojiSticker`
  - Presentation tool: `Laser Pointer`
  - Background modes: `Whiteboard` / `Blackboard`
  - Color + line width controls
  - Undo/Redo/Clear support
  - `Esc` to exit
- **Screen Recording**:
  - Start from capture toolbar (`Record` or `R`) or tray menu (`Record Full Screen`)
  - Adjustable region with Start/Cancel
  - Floating control bar with Pause/Resume/Stop/Cancel
  - MP4 (H.264) via native encoder (Media Foundation/AVFoundation); GIF and WebP via built-in encoders
  - Optional audio capture for MP4 (microphone/system audio/both; platform dependent)
- **Pin Windows**:
  - Borderless, always on top
  - Drag to move
  - Mouse wheel to zoom (with scale indicator)
  - Ctrl + mouse wheel to adjust opacity (with indicator)
  - Edge drag to resize
  - Rotation/flip via keyboard: `1` rotate CW, `2` rotate CCW, `3` flip horizontal, `4` flip vertical
  - Double-click or Esc to close
  - Context menu: Copy/Save/Open Cache Folder/OCR/QR Code Scan/Watermark/Click-through/Live Update/Close
  - **Annotation Toolbar**: Click the pencil icon to open annotation tools
    - Drawing tools: `Pencil` / `Marker` / `Arrow` / `Shape` / `Text` / `Mosaic` / `StepBadge` / `EmojiSticker` / `Eraser`
    - `Undo` / `Redo` support
    - `OCR` / `Copy` / `Save` quick actions
- **Settings Dialog**:
  - General tab: Launch at startup, toolbar style (Dark/Light), pin window opacity/zoom/cache settings, CLI install/uninstall
  - Hotkeys tab: Category-based hotkey management (Capture/Canvas/Clipboard/Pin/Recording)
  - Advanced tab: Auto blur configuration
  - Watermark tab: Image watermark, opacity, position, and scale
  - OCR tab: OCR language selection and post-recognition behavior
  - Recording tab: Frame rate, output format (MP4/GIF/WebP), quality, preview behavior, countdown, audio (enable/source/device)
  - Files tab: Screenshot/recording save paths, filename format
  - Updates tab: Auto-check, check frequency, last-checked status, manual check now
  - About tab: Version information
  - Automatic update checks run in the background
  - Settings stored via QSettings

## Tech Stack

- **Language**: C++17
- **Framework**: Qt 6 (Widgets/Gui/Svg/Concurrent/Network)
- **Build System**: CMake 3.16+ + Ninja
- **Dependencies** (auto-fetched via FetchContent):
  - [QHotkey](https://github.com/Skycoder42/QHotkey) (global hotkeys)
  - [OpenCV 4.10.0](https://github.com/opencv/opencv) (face/text detection pipeline)
  - [libwebp 1.3.2](https://github.com/webmproject/libwebp) (WebP animation encoding)
  - [ZXing-CPP v2.2.1](https://github.com/zxing-cpp/zxing-cpp) (QR/barcode processing)
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

- macOS 14.0+
- Qt 6.10.1 (Widgets/Gui/Svg/Concurrent/Network; Homebrew install recommended)
- Xcode Command Line Tools
- CMake 3.16+ and Ninja
- Git (for FetchContent dependencies)

### Windows

- Windows 10+
- Qt 6.10.1 (MSVC 2022 x64)
- Visual Studio 2022 Build Tools + Windows SDK
- CMake 3.16+ and Ninja
- Git (for FetchContent dependencies)

**PowerShell with MSVC**: If using PowerShell, load the Visual Studio environment first:

```powershell
Import-Module "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\Common7\Tools\Microsoft.VisualStudio.DevShell.dll"
Enter-VsDevShell -VsInstallPath "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools" -DevCmdArguments '-arch=x64' -SkipAutomaticLocation
```

## Build & Run

### Recommended Scripts

Use the scripts in `scripts/` for day-to-day development and CI-style verification.

**macOS:**

```bash
./scripts/build.sh                  # Debug build
./scripts/build-release.sh          # Release build
./scripts/run-tests.sh              # Build + run tests
./scripts/build-and-run.sh          # Debug build + run app
./scripts/build-and-run-release.sh  # Release build + run app
```

**Windows (cmd.exe or PowerShell with MSVC environment loaded):**

```batch
scripts\build.bat                   REM Debug build
scripts\build-release.bat           REM Release build
scripts\run-tests.bat               REM Build + run tests
scripts\build-and-run.bat           REM Debug build + run app
scripts\build-and-run-release.bat   REM Release build + run app
```

### Manual CMake (Advanced)

Use this only if you need custom configure flags.

**macOS (Debug):**

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build --parallel
# Output: build/bin/SnapTray-Debug.app
```

**macOS (Release):**

```bash
cmake -S . -B release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build release --parallel
# Output: release/bin/SnapTray.app
```

**Windows (Debug):**

```batch
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/msvc2022_64
cmake --build build --parallel
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

**Windows (Release):**

```batch
cmake -S . -B release -G Ninja -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/msvc2022_64
cmake --build release --parallel
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe --release release\bin\SnapTray.exe
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

**Windows:**

```batch
REM Prerequisites:
REM   - Qt 6: https://www.qt.io/download-qt-installer
REM   - NSIS: winget install NSIS.NSIS (for NSIS installer)
REM   - Windows 10 SDK: Included with Visual Studio (for MSIX package)
REM   - Visual Studio Build Tools or Visual Studio

REM Set Qt path (if not default location)
set QT_PATH=C:\Qt\6.10.1\msvc2022_64

packaging\windows\package.bat           REM Build both NSIS and MSIX
packaging\windows\package.bat nsis      REM Build NSIS installer only
packaging\windows\package.bat msix      REM Build MSIX package only

REM Output:
REM   dist\SnapTray-<version>-Setup.exe     (NSIS installer)
REM   dist\SnapTray-<version>.msix          (MSIX package)
REM   dist\SnapTray-<version>.msixupload    (for Store submission)
```

**MSIX Local Installation (Testing):**

Install the unsigned MSIX package locally for testing:

```powershell
Add-AppPackage -Path "dist\SnapTray-<version>.msix" -AllowUnsigned
```

To uninstall:

```powershell
Get-AppPackage SnapTray* | Remove-AppPackage
```

**Microsoft Store Submission:**

1. Sign in to [Partner Center](https://partner.microsoft.com)
2. Create a new app reservation or select existing app
3. Create a new submission
4. Upload the `.msixupload` file
5. Complete store listing, pricing, and age rating
6. Submit for certification

> **Note:** Store submission requires a Microsoft Partner Center account. The package is automatically signed by Microsoft during the submission process.

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
REM NSIS installer signing (requires Code Signing Certificate)
set CODESIGN_CERT=path\to\certificate.pfx
set CODESIGN_PASSWORD=your-password
packaging\windows\package.bat nsis

REM MSIX signing (for sideloading/enterprise deployment)
set MSIX_SIGN_CERT=path\to\certificate.pfx
set MSIX_SIGN_PASSWORD=your-password
set PUBLISHER_ID=CN=SnapTray Dev
packaging\windows\package.bat msix
```

> **Note:** For Microsoft Store distribution, MSIX signing is not required - Microsoft signs the package during the certification process.

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

| Optimization                  | Location       | Effect                           |
| ----------------------------- | -------------- | -------------------------------- |
| ccache/sccache auto-detection | CMakeLists.txt | 50-90% faster incremental builds |
| CI ccache (macOS)             | ci.yml         | 30-50% faster CI                 |
| CI sccache (Windows)          | ci.yml         | 30-50% faster CI                 |

**Note:** Precompiled Headers (PCH) and Unity Build are not used due to incompatibility with Objective-C++ files on macOS.

## Usage

1. After launch, the SnapTray icon appears in the system tray.
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
   - `OCR` (macOS/Windows): Recognize text in selection (copy directly or open editor based on OCR settings)
   - `QR Code Scan`: Detect and decode QR/barcodes in the selected region
   - `M` or `Multi-Region`: Toggle multi-region capture mode
   - `Undo/Redo`: `Ctrl+Z` / `Ctrl+Shift+Z` (macOS: `Cmd+Z` / `Cmd+Shift+Z`)
   - Annotations: Select a tool and drag within selection area
     - `Text`: Click to enter text
     - `StepBadge`: Click to place auto-numbered step markers
     - `Mosaic`: Drag brush to apply mosaic effect
     - `Eraser`: Drag to erase annotations
5. **Screen Recording**:
   - Use the control bar to Pause/Resume/Stop/Cancel
6. **Screen Canvas Mode**:
   - Toolbar provides drawing tools and laser pointer
   - Use `Whiteboard` / `Blackboard` buttons to switch canvas background mode
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
   - Context menu (Copy/Save/Open Cache Folder/OCR/QR Code Scan/Watermark/Click-through/Live Update/Close)
   - Double-click or `Esc` to close
   - Click pencil icon to open annotation toolbar for drawing on pinned images

## Command Line Interface (CLI)

SnapTray provides a CLI for scripting and automation.

### CLI Setup

**macOS:**
Open Settings → General → Install CLI to create the system-wide `snaptray` command. This requires administrator privileges.

**Windows (NSIS Installer):**
The installer automatically adds SnapTray to your system PATH. Open a new terminal after installation.

**Windows (MSIX/MS Store):**
The `snaptray` command is available immediately after installation via App Execution Alias.

### CLI Commands

| Command | Description | Requires Main Instance |
|---------|-------------|----------------------|
| `full` | Capture full screen | No |
| `screen` | Capture specified screen | No |
| `region` | Capture specified region | No |
| `gui` | Open region capture GUI | Yes |
| `canvas` | Toggle Screen Canvas | Yes |
| `record` | Start/stop/toggle recording | Yes |
| `pin` | Pin image to screen | Yes |
| `config` | View/modify settings (no options opens Settings dialog) | Partial |

### CLI Examples

```bash
# Help and version
snaptray --help
snaptray --version
snaptray full --help

# Local capture commands (no main instance needed)
snaptray full -c                      # Full screen to clipboard
snaptray full -d 1000 -o shot.png     # Delay 1s, then save
snaptray full -o screenshot.png       # Full screen to file
snaptray screen --list                # List available screens
snaptray screen 0 -c                  # Capture screen 0 (positional syntax)
snaptray screen -n 1 -o screen1.png   # Capture screen 1 (option syntax)
snaptray screen 1 -o screen1.png      # Capture screen 1 to file
snaptray region -r 0,0,800,600 -c     # Capture region on screen 0 to clipboard
snaptray region -n 1 -r 100,100,400,300 -o region.png
snaptray region -r 100,100,400,300 -o region.png

# IPC commands (requires main instance running)
snaptray gui                          # Open region capture selector
snaptray gui -d 2000                  # Open with 2 second delay
snaptray canvas                       # Toggle Screen Canvas mode
snaptray record start                 # Start recording
snaptray record stop                  # Stop recording
snaptray record                       # Toggle recording
snaptray record start -n 1            # Start full-screen recording on screen 1
snaptray pin -f image.png             # Pin image file
snaptray pin -c --center              # Pin from clipboard, centered
snaptray pin -f image.png -x 200 -y 120
snaptray config                        # Open Settings dialog (IPC)

# Config commands
snaptray config --list
snaptray config --get hotkey
snaptray config --set files/filenamePrefix SnapTray
snaptray config --reset

# Options
full/screen/region:
  -c, --clipboard    Copy to clipboard
  -o, --output       Save to file
  -p, --path         Save directory
  -d, --delay        Delay before capture (milliseconds)
  -n, --screen       Screen index
  -r, --region       Region coordinates (region only: x,y,width,height)
  --raw              Output raw PNG to stdout
  --cursor           Include mouse cursor

screen only:
  --list             List available screens

record:
  [action]           start | stop | toggle (default: toggle)

pin:
  -f, --file         Image file path
  -c, --clipboard    Pin from clipboard
  -x, --pos-x        Window X position
  -y, --pos-y        Window Y position
  --center           Center window on screen
```

### Return Codes

| Code | Meaning |
|------|---------|
| 0 | Success |
| 1 | General error |
| 2 | Invalid arguments |
| 3 | File error |
| 4 | Instance error (main app not running) |
| 5 | Recording error |

## Troubleshooting

### macOS: "SnapTray" cannot be opened because Apple cannot verify it

If you see the message:

- "SnapTray" cannot be opened because Apple could not verify it is free of malware

**Solution:** Remove the quarantine attribute using Terminal:

```bash
xattr -cr /Applications/SnapTray.app
```

This removes the quarantine flag that macOS adds to downloaded applications. After running this command, you should be able to open SnapTray normally.

### Windows: Application fails to start or shows missing DLL errors

If you see errors like:

- "The code execution cannot proceed because Qt6Core.dll was not found"
- "This application failed to start because no Qt platform plugin could be initialized"

**Solution:** Run windeployqt to deploy Qt dependencies:

```batch
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe build\bin\SnapTray-Debug.exe
```

Replace `C:\Qt\6.10.1\msvc2022_64` with your actual Qt installation path (should match the CMAKE_PREFIX_PATH you used during configuration).

## macOS Permissions

On first capture or recording, the system will request "Screen Recording" permission: `System Settings -> Privacy & Security -> Screen Recording` and enable SnapTray. Restart the app if necessary.

For window detection, "Accessibility" permission is required: `System Settings -> Privacy & Security -> Accessibility` and enable SnapTray.

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
|   |-- annotation/        # Annotation context and host adapter
|   |-- annotations/       # Annotation types (Arrow, Shape, Text, etc.)
|   |-- capture/           # Screen/audio capture engines
|   |-- cli/               # CLI handler, commands, IPC protocol
|   |-- colorwidgets/      # Custom color picker widgets
|   |-- cursor/            # Cursor state management
|   |-- detection/         # Face/text detection
|   |-- encoding/          # GIF/WebP encoders
|   |-- external/          # Third-party headers (msf_gif)
|   |-- hotkey/            # Hotkey manager and types
|   |-- input/             # Input abstractions (currently reserved)
|   |-- pinwindow/         # Pin window components
|   |-- region/            # Region selection components
|   |-- settings/          # Settings managers
|   |-- toolbar/           # Toolbar rendering
|   |-- tools/             # Tool handlers and registry
|   |-- ui/                # GlobalToast, IWidgetSection
|   |   `-- sections/      # Tool option panels
|   |-- update/            # Auto-update system
|   |-- utils/             # Utility helpers
|   |-- video/             # Video playback and recording UI
|   `-- widgets/           # Custom widgets (hotkey edit, dialogs)
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
|   |-- annotation/        # AnnotationContext implementation
|   |-- annotations/       # Annotation implementations
|   |-- capture/           # Capture engine implementations
|   |-- cli/               # CLI command implementations
|   |-- colorwidgets/      # Color widget implementations
|   |-- cursor/            # CursorManager implementation
|   |-- detection/         # Detection implementations
|   |-- encoding/          # Encoder implementations
|   |-- hotkey/            # HotkeyManager implementation
|   |-- input/             # Input tracking (platform-specific)
|   |-- pinwindow/         # Pin window component implementations
|   |-- platform/          # Platform abstraction (macOS/Windows)
|   |-- region/            # Region selection implementations
|   |-- settings/          # Settings manager implementations
|   |-- toolbar/           # Toolbar implementations
|   |-- tools/             # Tool system implementations
|   |-- ui/sections/       # UI section implementations
|   |-- update/            # Auto-update implementations
|   |-- utils/             # Utility implementations
|   |-- video/             # Video component implementations
|   `-- widgets/           # Widget implementations
|-- resources/
|   |-- resources.qrc
|   |-- icons/
|   |   |-- snaptray.svg
|   |   |-- snaptray.png
|   |   |-- snaptray.icns
|   |   `-- snaptray.ico
|   `-- cascades/          # OpenCV Haar cascade files
|-- scripts/
|   |-- build.sh / build.bat
|   |-- build-release.sh / build-release.bat
|   |-- build-and-run.sh / build-and-run.bat
|   |-- build-and-run-release.sh / build-and-run-release.bat
|   `-- run-tests.sh / run-tests.bat
|-- tests/
|   |-- Annotations/
|   |-- CLI/
|   |-- Detection/
|   |-- Encoding/
|   |-- Hotkey/
|   |-- IPC/
|   |-- PinWindow/
|   |-- RecordingManager/
|   |-- RegionSelector/
|   |-- Settings/
|   |-- ToolOptionsPanel/
|   |-- Tools/
|   |-- UISections/
|   |-- Update/
|   |-- Utils/
|   `-- Video/
`-- packaging/
    |-- macos/
    |   |-- package.sh
    |   `-- entitlements.plist
    `-- windows/
        |-- package.bat
        |-- package-nsis.bat
        |-- package-msix.bat
        |-- installer.nsi
        |-- license.txt
        |-- AppxManifest.xml.in
        |-- generate-assets.ps1
        `-- assets/
```

## Architecture

The codebase follows a modular architecture with extracted components for maintainability:

### Extracted Components

| Component                   | Location           | Responsibility                            |
| --------------------------- | ------------------ | ----------------------------------------- |
| `CursorManager`             | `src/cursor/`      | Centralized cursor state management       |
| `MagnifierPanel`            | `src/region/`      | Magnifier rendering and caching           |
| `UpdateThrottler`           | `src/region/`      | Event throttling logic                    |
| `TextAnnotationEditor`      | `src/region/`      | Text annotation editing/transformation    |
| `SelectionStateManager`     | `src/region/`      | Selection state and operations            |
| `RegionExportManager`       | `src/region/`      | Region export (copy/save) handling        |
| `RegionInputHandler`        | `src/region/`      | Input event handling for region selection |
| `RegionPainter`             | `src/region/`      | Region rendering logic                    |
| `MultiRegionManager`        | `src/region/`      | Multi-region capture coordination         |
| `AnnotationSettingsManager` | `src/settings/`    | Centralized annotation settings           |
| `FileSettingsManager`       | `src/settings/`    | File path settings                        |
| `PinWindowSettingsManager`  | `src/settings/`    | Pin window settings                       |
| `AutoBlurSettingsManager`   | `src/settings/`    | Auto blur feature settings                |
| `WatermarkSettingsManager`  | `src/settings/`    | Watermark settings                        |
| `ImageTransformer`          | `src/pinwindow/`   | Image rotation/flip/scale                 |
| `ResizeHandler`             | `src/pinwindow/`   | Window edge resize                        |
| `UIIndicators`              | `src/pinwindow/`   | Scale/opacity/click-through indicators    |
| `ClickThroughExitButton`    | `src/pinwindow/`   | Exit button for click-through mode        |
| `PinHistoryStore`           | `src/pinwindow/`   | Pin history persistence                   |
| `PinHistoryWindow`          | `src/pinwindow/`   | Pin history UI                            |
| `FaceDetector`              | `src/detection/`   | Face detection for auto-blur              |
| `AutoBlurManager`           | `src/detection/`   | Auto-blur orchestration                   |
| `HotkeyManager`             | `src/hotkey/`      | Centralized hotkey registration           |
| `CLIHandler`                | `src/cli/`         | CLI command parsing and dispatch          |
| `UpdateChecker`             | `src/update/`      | Auto-update version checking              |
| `UpdateSettingsManager`     | `src/update/`      | Update preferences and scheduling         |
| `AnnotationContext`         | `src/annotation/`  | Shared annotation execution context       |
| Color widgets               | `src/colorwidgets/`| Custom color picker dialog and components |
| Section classes             | `src/ui/sections/` | Tool option panel components              |

### Test Suite

The Qt Test suite currently covers:

- Tool options and UI sections
- Region selector and pin window behaviors
- Recording lifecycle and integration paths
- Encoding and detection modules
- CLI/IPC flows
- Settings, hotkeys, update components, and utilities
- Video timeline components

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

## License

MIT License
