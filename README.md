# SnapTray - System Tray Screenshot Tool

**English** | [繁體中文](README.zh-TW.md)

SnapTray is a lightweight screenshot utility that lives in your system tray. Press F2 (default) to capture a region, then pin it to your screen, copy, or save.

## Features

- **System Tray Menu**: `Region Capture` (shows current hotkey), `Screen Canvas`, `Close All Pins`, `Settings`, `Exit`
- **Global Hotkeys**: Customizable in settings with live hotkey registration
  - Region Capture: Default `F2`
  - Screen Canvas: Double-tap `F2` (press twice quickly)
- **Region Capture Overlay**:
  - Crosshair + magnifier (pixel-level precision)
  - RGB/HEX color preview (Shift to toggle, C to copy color code)
  - Dimension display
  - Selection handles (Snipaste-style)
  - Window detection (macOS/Windows): Auto-detect window under cursor, single-click to select
- **Capture Toolbar**:
  - `Selection` tool (adjust selection area)
  - `Pin` to screen (Enter)
  - `Save` to file (Ctrl+S / Cmd+S on macOS)
  - `Copy` to clipboard (Ctrl+C / Cmd+C on macOS)
  - `Cancel` (Esc)
  - Annotation tools: `Arrow` / `Pencil` / `Marker` / `Rectangle` / `Text` / `Mosaic` / `StepBadge` (with Undo/Redo)
  - `OCR` text recognition (macOS/Windows, supports Traditional Chinese, Simplified Chinese, English)
  - Color picker for annotation tools
- **Screen Canvas**:
  - Full-screen annotation mode, draw directly on screen
  - Drawing tools: `Pencil` / `Marker` / `Arrow` / `Rectangle`
  - Color picker for drawing
  - Undo/Redo/Clear support
  - `Esc` to exit
- **Pin Windows**:
  - Borderless, always on top
  - Drag to move
  - Mouse wheel to zoom (with scale indicator)
  - Edge drag to resize
  - Rotation support (via context menu)
  - Double-click or Esc to close
  - Context menu: Save/Copy/OCR/Zoom/Rotate/Close
- **Settings Dialog**:
  - General tab: Launch at startup
  - Hotkeys tab: Custom region capture hotkey (double-tap same hotkey for Screen Canvas)
  - Settings stored via QSettings

## Tech Stack

- **Language**: C++17
- **Framework**: Qt 6 (Widgets/Gui)
- **Build System**: CMake 3.16+
- **Dependencies**: [QHotkey](https://github.com/Skycoder42/QHotkey) (auto-fetched via FetchContent)
- **macOS Native Frameworks**:
  - CoreGraphics / ApplicationServices (window detection)
  - AppKit (system integration)
  - Vision (OCR text recognition)

## System Requirements

### macOS
- macOS 10.15+
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

### Development Build (Debug)

**macOS:**
```bash
cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
open build/SnapTray.app
```

**Windows:**
```batch
# Step 1: Configure (replace with your Qt path)
cmake -S . -B build -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/msvc2022_64

# Step 2: Build
cmake --build build

# Step 3: Deploy Qt dependencies (required to run the executable)
# Replace with your Qt installation path
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe build\SnapTray.exe

# Step 4: Run
build\SnapTray.exe
```

**Note:** Windows development builds require running `windeployqt` to copy Qt runtime DLLs (Qt6Core.dll, qwindows.dll platform plugin, etc.) alongside the executable. This step is automated in the packaging script for release builds.

### Release Build

**macOS:**
```bash
cmake -S . -B release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build release
# Output: release/SnapTray.app
```

**Windows:**
```batch
cmake -S . -B release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH=C:/Qt/6.10.1/msvc2022_64
cmake --build release --config Release

# Deploy Qt dependencies
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe --release release\Release\SnapTray.exe

# Output: release\Release\SnapTray.exe
```

**Note:** For distribution, use the packaging scripts (see below) which automate the deployment process.

### Packaging

The packaging scripts automatically perform Release builds, deploy Qt dependencies, and create installers.

**macOS (DMG):**
```bash
# Prerequisites: brew install qt (if not installed)
# Optional: brew install create-dmg (for prettier DMG)

./packaging/macos/package.sh
# Output: dist/SnapTray-1.0.0-macOS.dmg
```

**Windows (NSIS Installer):**
```batch
REM Prerequisites:
REM   - Qt 6: https://www.qt.io/download-qt-installer
REM   - NSIS: winget install NSIS.NSIS
REM   - Visual Studio Build Tools or Visual Studio

REM Set Qt path (if not default location)
set QT_PATH=C:\Qt\6.6.1\msvc2019_64

packaging\windows\package.bat
REM Output: dist\SnapTray-1.0.0-Setup.exe
```

#### Code Signing (Optional)

Unsigned installers will show warnings during installation:
- macOS: "Cannot verify developer" (requires right-click → Open)
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

## Usage

1. After launch, a green square icon appears in the system tray.
2. Press the region capture hotkey (default `F2`) to enter capture mode; or double-tap (press `F2` twice quickly) for Screen Canvas mode.
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
   - `Esc` or `Cancel`: Cancel selection
   - Annotations: Select a tool and drag within selection area
     - `Text`: Click to enter text
     - `StepBadge`: Click to place auto-numbered step markers
     - `Mosaic`: Drag brush to apply mosaic effect
   - `OCR` (macOS/Windows): Recognize text in selection and copy to clipboard
   - `Undo/Redo`: `Ctrl+Z` / `Ctrl+Shift+Z` (macOS: `Cmd+Z` / `Cmd+Shift+Z`)
5. **Screen Canvas Mode**:
   - Toolbar provides `Pencil` / `Marker` / `Arrow` / `Rectangle` drawing tools
   - Click color picker to change drawing color
   - `Undo` / `Redo`: Undo/redo annotations
   - `Clear`: Clear all annotations
   - `Esc` or `Exit`: Leave Screen Canvas mode
6. **Pin Window Actions**:
   - Drag to move
   - Mouse wheel to zoom
   - Edge drag to resize
   - Context menu (Save/Copy/OCR/Zoom In/Zoom Out/Reset Zoom/Rotate CW/Rotate CCW/Close)
   - Double-click or `Esc` to close

## Troubleshooting

### Windows: Application fails to start or shows missing DLL errors

If you see errors like:
- "The code execution cannot proceed because Qt6Core.dll was not found"
- "This application failed to start because no Qt platform plugin could be initialized"

**Solution:** Run windeployqt to deploy Qt dependencies:
```batch
C:\Qt\6.10.1\msvc2022_64\bin\windeployqt.exe build\SnapTray.exe
```

Replace `C:\Qt\6.10.1\msvc2022_64` with your actual Qt installation path (should match the CMAKE_PREFIX_PATH you used during configuration).

## macOS Permissions

On first capture, the system will request "Screen Recording" permission: `System Preferences → Privacy & Security → Screen Recording` and enable SnapTray. Restart the app if necessary.

For window detection, "Accessibility" permission is required: `System Preferences → Privacy & Security → Accessibility` and enable SnapTray.

## Project Structure

```
snap/
├── CMakeLists.txt
├── Info.plist
├── README.md
├── include/
│   ├── MainApplication.h
│   ├── SettingsDialog.h
│   ├── AutoLaunchManager.h
│   ├── CaptureManager.h
│   ├── RegionSelector.h
│   ├── ScreenCanvas.h
│   ├── ScreenCanvasManager.h
│   ├── PinWindow.h
│   ├── PinWindowManager.h
│   ├── AnnotationLayer.h
│   ├── AnnotationController.h
│   ├── ToolbarWidget.h
│   ├── ColorPaletteWidget.h
│   ├── ColorPickerDialog.h
│   ├── LineWidthWidget.h        # Line width adjustment widget
│   ├── IconRenderer.h
│   ├── InlineTextEditor.h       # Inline text editor widget
│   ├── MagnifierOverlay.h       # Magnifier/crosshair widget
│   ├── SelectionController.h    # Selection control widget
│   ├── PlatformFeatures.h
│   ├── WindowDetector.h
│   ├── WindowDetectionOverlay.h # macOS window detection overlay
│   ├── OCRManager.h
│   └── OCRController.h          # macOS OCR controller
├── src/
│   ├── main.cpp
│   ├── MainApplication.cpp
│   ├── SettingsDialog.cpp
│   ├── AutoLaunchManager_mac.mm # macOS
│   ├── AutoLaunchManager_win.cpp # Windows
│   ├── CaptureManager.cpp
│   ├── RegionSelector.cpp
│   ├── ScreenCanvas.cpp
│   ├── ScreenCanvasManager.cpp
│   ├── PinWindow.cpp
│   ├── PinWindowManager.cpp
│   ├── AnnotationLayer.cpp
│   ├── AnnotationController.cpp
│   ├── ToolbarWidget.cpp
│   ├── ColorPaletteWidget.cpp
│   ├── LineWidthWidget.cpp
│   ├── ColorPickerDialog.mm     # macOS native color picker
│   ├── IconRenderer.cpp
│   ├── InlineTextEditor.cpp
│   ├── MagnifierOverlay.cpp
│   ├── SelectionController.cpp
│   ├── WindowDetector.mm        # macOS
│   ├── WindowDetector_win.cpp   # Windows
│   ├── WindowDetectionOverlay.cpp # macOS
│   ├── OCRManager.mm            # macOS
│   ├── OCRManager_win.cpp       # Windows
│   ├── OCRController.cpp        # macOS
│   └── platform/
│       ├── WindowLevel.h
│       ├── WindowLevel_mac.mm
│       ├── WindowLevel_win.cpp
│       ├── PlatformFeatures_mac.mm
│       └── PlatformFeatures_win.cpp
├── resources/
│   ├── resources.qrc
│   ├── snaptray.rc              # Windows resource file
│   └── icons/
│       ├── snaptray.svg         # Original icon
│       ├── snaptray.png         # 1024x1024 PNG
│       ├── snaptray.icns        # macOS icon
│       └── snaptray.ico         # Windows icon
└── packaging/
    ├── macos/
    │   ├── package.sh           # macOS packaging script
    │   └── entitlements.plist   # Code signing entitlements
    └── windows/
        ├── package.bat          # Windows packaging script
        ├── installer.nsi        # NSIS installer script
        └── license.txt          # License text
```

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
- Window detection and OCR are supported on macOS and Windows.

## License

MIT License
