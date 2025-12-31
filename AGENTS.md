# SnapTray Project

A Qt6-based screenshot and screen recording application for Windows and macOS.

## Build Instructions

**Windows**: Use `cmd.exe` to execute build commands, not bash.

### Debug Build (Development)

Use Debug build during development for better debugging experience.

**Windows**:

```batch
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug
cmake --build build
```

**macOS**:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
```

### Release Build (Production Testing)

Use Release build to test production behavior before packaging.

**Windows**:

```batch
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build
```

**macOS**:

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
cmake --build build
```

### Packaging (Release)

Use the packaging scripts in `packaging/` for distribution builds (automatically uses Release):

**macOS** - Creates signed DMG:

```bash
./packaging/macos/package.sh
```

**Windows** - Creates NSIS installer:

```batch
packaging\windows\package.bat
```

### Running Tests

```bash
cd build && ctest --output-on-failure
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
- `tests/` - Unit tests (Qt Test framework)

## Key Features

- Screen capture with region selection
- Screen recording (MP4 via Media Foundation on Windows, AVFoundation on macOS)
- Annotation tools (pencil, marker, arrow, shapes, mosaic, step badges)
- Pin windows for captured screenshots
- Global hotkey support (via QHotkey)

## Architecture Patterns

### Extracted Components

The codebase follows a modular architecture. When refactoring, prefer extracting components:

| Component                   | Location         | Responsibility                         |
| --------------------------- | ---------------- | -------------------------------------- |
| `MagnifierPanel`            | `src/region/`    | Magnifier rendering and caching        |
| `UpdateThrottler`           | `src/region/`    | Event throttling logic                 |
| `TextAnnotationEditor`      | `src/region/`    | Text annotation editing/transformation |
| `SelectionStateManager`     | `src/region/`    | Selection state and operations         |
| `RadiusSliderWidget`        | `src/region/`    | Radius slider control for tools        |
| `AnnotationSettingsManager` | `src/settings/`  | Centralized annotation settings        |
| `ImageTransformer`          | `src/pinwindow/` | Image rotation/flip/scale              |
| `ResizeHandler`             | `src/pinwindow/` | Window edge resize                     |
| `UIIndicators`              | `src/pinwindow/` | Scale/opacity/click-through indicators |
| `ClickThroughExitButton`    | `src/pinwindow/` | Exit button for click-through mode     |

### Data-Driven Patterns

Use lookup tables instead of switch statements for tool capabilities:

```cpp
// Good: Data-driven lookup table
static const std::map<ToolbarButton, ToolCapabilities> kToolCapabilities = {
    {ToolbarButton::Pencil, {true, true, true}},
    {ToolbarButton::Marker, {true, false, true}},
    // ...
};

bool shouldShowColorPalette() const {
    auto it = kToolCapabilities.find(m_currentTool);
    return it != kToolCapabilities.end() && it->second.showInPalette;
}

// Bad: Repetitive switch statements
bool shouldShowColorPalette() const {
    switch (m_currentTool) {
    case ToolbarButton::Pencil:
    case ToolbarButton::Marker:
        return true;
    default:
        return false;
    }
}
```

### Shared Settings

Use `AnnotationSettingsManager` for annotation-related settings:

```cpp
// Good: Use shared manager
auto& settings = AnnotationSettingsManager::instance();
QColor color = settings.loadColor();
settings.saveColor(newColor);

// Bad: Direct QSettings access in each file
QSettings settings("Victor Fu", "SnapTray");
QColor color = settings.value("annotationColor", Qt::red).value<QColor>();
```

### Glass Style (macOS HIG)

The UI follows macOS Human Interface Guidelines with a glass/frosted effect. Use the `GlassRenderer` class for all floating panels.

#### Core Components

| Component            | Location                 | Responsibility                          |
| -------------------- | ------------------------ | --------------------------------------- |
| `GlassRenderer`      | `src/GlassRenderer.cpp`  | Static utility for drawing glass panels |
| `ToolbarStyleConfig` | `include/ToolbarStyle.h` | Theme configuration (Dark/Light)        |

#### Usage

```cpp
// Good: Use GlassRenderer for floating panels
void MyWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    auto config = ToolbarStyleConfig::getStyle(ToolbarStyleConfig::loadStyle());
    GlassRenderer::drawGlassPanel(painter, rect(), config);
}

// Bad: Manual painting without glass effect
void MyWidget::paintEvent(QPaintEvent*) {
    QPainter painter(this);
    painter.fillRect(rect(), Qt::white);  // No glass effect
}
```

#### Design Principles

- **Subtle blur simulation** - Use gradient to simulate frosted glass (Qt doesn't have native blur)
- **Low-opacity borders** - 1px hairline borders with ~8% opacity
- **Soft shadows** - Multi-layer shadows with decreasing opacity
- **Theme support** - Always use `ToolbarStyleConfig` for colors, never hardcode

## Development Guidelines

### Logging

Use `qDebug()` for development diagnostics only. Release builds automatically suppress `qDebug()` output via `QT_NO_DEBUG_OUTPUT`.

| Function      | Purpose                                                    | Release Behavior     |
| ------------- | ---------------------------------------------------------- | -------------------- |
| `qDebug()`    | Development diagnostics, state tracking, defensive cleanup | **Suppressed**       |
| `qInfo()`     | General info (user may need to know)                       | Output               |
| `qWarning()`  | Warnings: hardware/API failures, features unavailable      | Output               |
| `qCritical()` | Critical errors: may cause malfunction                     | Output               |
| `qFatal()`    | Fatal errors: program must terminate                       | Output and terminate |

```cpp
// Good: Use qDebug for development diagnostics
qDebug() << "RecordingManager: Starting capture timer...";
qDebug() << "Previous encoder still exists, cleaning up";  // Defensive cleanup

// Good: Use qWarning for real errors
qWarning() << "Failed to create D3D11 device:" << hr;
qWarning() << "Audio capture not available on this platform";

// Bad: Using qWarning for dev messages (will output in Release)
qWarning() << "Already running";  // Should use qDebug
qWarning() << "Invalid frame rate, using default";  // Should use qDebug
```

### Code Style

- Use C++17 features
- Prefer `auto` for complex types, explicit types for primitives
- Use `nullptr` instead of `NULL`
- Use range-based for loops when possible
- Prefer `QPoint`/`QRect` over separate x/y/width/height variables

### Testing

- Write tests for extracted components
- Test files go in `tests/<ComponentName>/tst_<TestName>.cpp`
- Use Qt Test framework (`QCOMPARE`, `QVERIFY`)
- Run tests after each significant change

### Refactoring Checklist

When refactoring large files:

1. Identify repeated patterns (3+ occurrences)
2. Extract to helper methods or separate classes
3. Use data-driven approaches for switch statements
4. Write tests for extracted components
5. Update CMakeLists.txt for new files
6. Run all tests to verify no regressions

### Common Patterns to Avoid

- Avoid creating QSettings objects in multiple places (use `getSettings()` or `AnnotationSettingsManager`)
- Avoid duplicate coordinate conversion code (use `localToGlobal()`/`globalToLocal()`)
- Avoid repetitive tool capability checks (use lookup tables)
- Avoid inline magic numbers (use named constants)

## Test Coverage

| Component           | Test Count |
| ------------------- | ---------- |
| ColorAndWidthWidget | 113        |
| RegionSelector      | 108        |
| RecordingManager    | 58         |
| Encoding            | 43         |
| Detection           | 42         |
| PinWindow           | 24         |
| Audio               | 23         |
| **Total**           | **411**    |
