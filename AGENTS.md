# SnapTray Project

A Qt6-based screenshot and screen recording application for Windows and macOS.

## Build Instructions

### Windows

**Important:** On Windows, use `cmd.exe` to execute build commands. Do not use bash-style `cd` syntax.

```batch
# Configure (from project root)
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build build
```

### macOS

```bash
# Configure
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"

# Build
cmake --build build
```

### Running Tests

```bash
# Run all tests
cd build && ctest --output-on-failure

# Run specific test
./build/RegionSelector_SelectionStateManager
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

| Component | Location | Responsibility |
|-----------|----------|----------------|
| `MagnifierPanel` | `src/region/` | Magnifier rendering and caching |
| `UpdateThrottler` | `src/region/` | Event throttling logic |
| `TextAnnotationEditor` | `src/region/` | Text annotation editing/transformation |
| `SelectionStateManager` | `src/region/` | Selection state and operations |
| `AnnotationSettingsManager` | `src/settings/` | Centralized annotation settings |
| `ImageTransformer` | `src/pinwindow/` | Image rotation/flip/scale |
| `ResizeHandler` | `src/pinwindow/` | Window edge resize |
| `UIIndicators` | `src/pinwindow/` | Scale/opacity/click-through indicators |

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

## Development Guidelines

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

| Component | Test Count |
|-----------|------------|
| ColorAndWidthWidget | 87 |
| PinWindow | 49 |
| RegionSelector | 74 |
| **Total** | **210** |
