# SnapTray Project

A Qt6-based screenshot and screen recording application for Windows and macOS.

## Build Instructions

**Windows**: Use `cmd.exe` or PowerShell (with MSVC environment loaded).

Use scripts in `scripts/` for building and testing:

```batch
scripts\build.bat                   # Debug build only
scripts\build-release.bat           # Release build only
scripts\build-and-run.bat           # Debug build + run
scripts\build-and-run-release.bat   # Release build + run
scripts\run-tests.bat               # Run tests
```

```bash
# macOS
./scripts/build.sh                  # Debug build only
./scripts/build-release.sh          # Release build only
./scripts/build-and-run.sh          # Debug build + run
./scripts/build-and-run-release.sh  # Release build + run
./scripts/run-tests.sh              # Run tests
```

**For coding agents**: Use `build.bat` (Windows) or `build.sh` (macOS) to verify changes compile successfully. Running the application (`build-and-run`) is not necessary for validation.

### Packaging

```batch
# Windows
packaging\windows\package.bat           # Build both NSIS and MSIX
packaging\windows\package.bat nsis      # NSIS installer only
packaging\windows\package.bat msix      # MSIX package only
```

```bash
# macOS - Creates signed DMG
./packaging/macos/package.sh
```

### Prerequisites

- Qt 6.10.1 (Widgets, Gui, Svg, Concurrent)
- CMake 3.16+
- Ninja build system
- Windows: Visual Studio 2022 Build Tools, Windows SDK (DXGI, Media Foundation, WASAPI)
- macOS: Xcode Command Line Tools

## Project Structure

```
src/
├── annotations/     # 13 annotation types (Pencil, Arrow, Shape, Mosaic, etc.)
├── capture/         # Screen capture (DXGICaptureEngine, SCKCaptureEngine)
├── detection/       # Face/text detection, auto-blur
├── encoding/        # GIF/WebP encoders
├── pinwindow/       # Pin window components
├── platform/        # Platform abstraction (WindowLevel, PlatformFeatures)
├── recording/       # Recording effects (Spotlight, CursorHighlight)
├── region/          # Region selection UI
├── scrolling/       # Scrolling capture and image stitching
├── settings/        # Settings managers
├── stitching/       # Feature matching, optical flow
├── toolbar/         # Toolbar rendering
├── tools/           # Tool handlers and registry
├── ui/sections/     # Tool option panels
└── video/           # Video playback and recording UI
```

## Architecture Patterns

### Settings Managers (Singleton)

Use the appropriate settings manager instead of direct QSettings:

```cpp
// Annotation settings (color, width, mosaic type, etc.)
auto& settings = AnnotationSettingsManager::instance();
settings.loadColor();
settings.saveColor(newColor);

// File paths (screenshot/recording locations)
FileSettingsManager::instance();

// Pin window settings (opacity, zoom)
PinWindowSettingsManager::instance();

// Other settings
QSettings settings = getSettings();  // From include/settings/Settings.h
```

### Tool System

Tools use `ToolId` enum and are registered in `ToolRegistry`. Tool handlers implement `IToolHandler`.

```cpp
// Tool definitions in include/tools/
ToolId.h          // Unified enum for all tools
ToolDefinition.h  // Tool metadata
IToolHandler.h    // Handler interface
ToolRegistry.h    // Tool registration
ToolManager.h     // Active tool management
```

### Data-Driven Patterns

Use lookup tables instead of switch statements:

```cpp
// Good: Data-driven lookup
static const std::map<ToolId, ToolCapabilities> kToolCapabilities = {
    {ToolId::Pencil, {true, true, true}},
    {ToolId::Marker, {true, false, true}},
};

// Bad: Repetitive switch statements
switch (toolId) { case ToolId::Pencil: ... }
```

### Glass Style UI

Use `GlassRenderer` for floating panels (macOS HIG style):

```cpp
void MyWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    auto config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    GlassRenderer::drawGlassPanel(painter, rect(), config);
}
```

## Platform-Specific Code

| Feature | Windows | macOS |
|---------|---------|-------|
| Screen Capture | `DXGICaptureEngine` | `SCKCaptureEngine` |
| Video Encoding | `MediaFoundationEncoder` | `AVFoundationEncoder` |
| Audio Capture | `WASAPIAudioCaptureEngine` | `CoreAudioCaptureEngine` |
| Window Detection | `WindowDetector_win.cpp` | `WindowDetector.mm` |
| OCR | `OCRManager_win.cpp` | `OCRManager.mm` |

## Development Guidelines

### Logging

| Function | Purpose | Release |
|----------|---------|---------|
| `qDebug()` | Development diagnostics | **Suppressed** |
| `qWarning()` | Hardware/API failures | Output |
| `qCritical()` | Critical errors | Output |

### Code Style

- C++17 features
- `auto` for complex types, explicit for primitives
- `nullptr` instead of `NULL`
- `QPoint`/`QRect` over separate x/y/width/height

### Testing

- Test files: `tests/<ComponentName>/tst_<TestName>.cpp`
- Qt Test framework (`QCOMPARE`, `QVERIFY`)
- Run `scripts\run-tests.bat` after changes

### Common Patterns to Avoid

- Direct QSettings access (use settings managers)
- Repetitive switch statements (use lookup tables)
- Inline magic numbers (use named constants)
