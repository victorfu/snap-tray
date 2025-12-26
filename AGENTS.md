# SnapTray Project

A Qt6-based screenshot and screen recording application for Windows and macOS.

## Build Instructions

### Windows

**Important:** On Windows, use `cmd.exe` to execute build commands. Do not use bash-style `cd` syntax.

```batch
# Configure (from project root)
cmd.exe /c "cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release"

# Build
cmd.exe /c "cd /d d:\Documents\snap-tray\build && cmake --build ."

```

### macOS

```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build

# Or use make directly
cd build && make
```

### Prerequisites

- Qt6 (Widgets, Gui, Svg)
- CMake 3.16+
- Ninja or Visual Studio generator
- Windows SDK (for DXGI, Media Foundation, WASAPI)

## Project Structure

- `src/` - Source files
- `include/` - Header files
- `resources/` - Qt resources (icons, qrc)
- `cmake/` - CMake templates (version.h.in, snaptray.rc.in)

## Key Features

- Screen capture with region selection
- Screen recording (MP4 via Media Foundation on Windows, AVFoundation on macOS)
- Annotation tools (pencil, marker, arrow, shapes, mosaic, step badges)
- Pin windows for captured screenshots
- Global hotkey support (via QHotkey)
