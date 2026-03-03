# SnapTray Project

A Qt6-based screenshot and screen recording application for Windows and macOS. Provides region capture, on-screen annotations, screen canvas mode, and high-quality video recording with optional audio capture.

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

- Qt 6.10.1 (Widgets, Gui, Svg, Concurrent, Network)
- CMake 3.16+
- Ninja build system
- Windows: Visual Studio 2022 Build Tools, Windows SDK (DXGI, Media Foundation, WASAPI)
- macOS: Xcode Command Line Tools

### External Dependencies (auto-fetched via CMake)

- **QHotkey**: Global hotkey support
- **OpenCV 4.10.0**: Face detection (static, minimal build)
- **libwebp 1.3.2**: WebP animation encoding
- **ZXing-CPP v2.2.1**: QR code processing

## Project Structure

```
snap-tray/
├── include/                    # Public headers
│   ├── annotation/            # AnnotationContext, AnnotationHostAdapter
│   ├── annotations/           # Annotation types and classes
│   ├── beautify/              # Beautify panel/settings interfaces
│   ├── capture/               # Capture engine interfaces
│   ├── cli/                   # CLI handler, commands, IPC protocol
│   │   └── commands/          # Individual CLI command classes
│   ├── colorwidgets/          # Custom color picker widgets
│   ├── cursor/                # Cursor management
│   ├── detection/             # Face/text/credential detection
│   ├── encoding/              # Video/GIF/WebP encoders
│   ├── external/              # Third-party headers (msf_gif)
│   ├── hotkey/                # HotkeyManager, HotkeyTypes
│   ├── mcp/                   # Built-in MCP server and tool contracts (debug-only)
│   ├── pinwindow/             # Pin window components
│   ├── platform/              # Platform abstraction (WindowLevel, PlatformFeatures)
│   ├── region/                # Region selection UI
│   ├── settings/              # Settings managers
│   ├── share/                 # Share upload client
│   ├── toolbar/               # Toolbar rendering
│   ├── tools/                 # Tool system (ToolId, IToolHandler)
│   │   └── handlers/          # Tool handler interfaces
│   ├── ui/                    # GlobalToast, IWidgetSection
│   │   └── sections/          # Tool option panels
│   ├── update/                # UpdateChecker, UpdateDialog, UpdateSettingsManager
│   ├── utils/                 # Utility helpers
│   ├── video/                 # Video playback UI
│   ├── widgets/               # Custom widgets (hotkey edit, dialogs)
│   └── Recording*.h           # Recording headers at include root
│
├── src/                       # Implementation files
│   ├── annotation/            # AnnotationContext implementation
│   ├── annotations/           # Annotation implementations
│   ├── beautify/              # Beautify implementations
│   ├── capture/               # Capture engine implementations
│   ├── cli/                   # CLI command implementations
│   ├── colorwidgets/          # Color widget implementations
│   ├── cursor/                # Cursor manager
│   ├── detection/             # Detection algorithms
│   ├── encoding/              # Encoder implementations
│   ├── hotkey/                # HotkeyManager implementation
│   ├── mcp/                   # MCP HTTP transport and tool implementations (debug-only)
│   ├── pinwindow/             # Pin window logic
│   ├── platform/              # Platform-specific code
│   ├── region/                # Region selection implementation
│   ├── settings/              # Settings management
│   ├── share/                 # Share upload client implementation
│   ├── toolbar/               # Toolbar rendering
│   ├── tools/handlers/        # Tool handler implementations
│   ├── ui/sections/           # UI sections implementation
│   ├── update/                # Auto-update implementations
│   ├── utils/                 # Utility implementations
│   ├── video/                 # Video components
│   ├── widgets/               # Custom widgets
│   └── Recording*.cpp         # Recording implementation files at src root
│
├── tests/                     # Test suite (Qt Test Framework)
│   ├── Annotations/           # Annotation type tests
│   ├── Beautify/              # Beautify settings/renderer tests
│   ├── CLI/                   # CLI command tests
│   ├── Detection/             # FaceDetector, AutoBlurManager, TableDetector tests
│   ├── Encoding/              # NativeGifEncoder, EncoderFactory tests
│   ├── Hotkey/                # HotkeyManager tests
│   ├── IPC/                   # SingleInstanceGuard tests
│   ├── MCP/                   # MCP server protocol and tools tests
│   ├── PinWindow/             # Transform, Resize, PinHistory, PinMerge, RegionLayout tests
│   ├── RecordingManager/      # StateMachine, Lifecycle, InitTask tests
│   ├── RegionSelector/        # MagnifierPanel, Throttler tests
│   ├── Settings/              # SettingsManager tests
│   ├── Share/                 # ShareUploadClient tests
│   ├── ToolOptionsPanel/      # State, Signals, HitTest, Events tests
│   ├── Tools/                 # ToolRegistry, ToolHandler tests
│   ├── UISections/            # UI section tests
│   ├── Update/                # UpdateChecker, InstallSourceDetector tests
│   ├── Utils/                 # CoordinateHelper tests
│   └── Video/                 # Video timeline tests
│
├── packaging/                 # Application packaging
│   ├── windows/               # NSIS and MSIX packaging
│   └── macos/                 # DMG packaging
│
├── resources/                 # Application resources
│   ├── resources.qrc          # Qt resource file
│   ├── icons/                 # Icon files
│   └── cascades/              # OpenCV Haar cascade files
│
├── scripts/                   # Build and test scripts
├── cmake/                     # CMake templates (version.h, Info.plist)
└── CMakeLists.txt             # Main CMake configuration
```

## Library Architecture

The project uses a modular static library architecture:

1. **snaptray_core**: Annotations, settings, cursor management, utilities
2. **snaptray_colorwidgets**: Custom color picker dialog and components
3. **snaptray_algorithms**: Detection algorithms (face detection, auto-blur) - depends on OpenCV
4. **snaptray_platform**: Platform-specific capture, encoding, video playback
5. **snaptray_ui**: UI components, toolbar, region selector, pin windows, tool system, recording workflow
6. **snaptray_mcp**: MCP HTTP server and tool handlers (debug-only, linked conditionally)
7. **SnapTray**: Main executable linking all libraries

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

// Auto-blur settings
AutoBlurSettingsManager::instance();

// OCR settings (language, region)
OCRSettingsManager::instance();

// Watermark configuration
WatermarkSettingsManager::instance();

// Update preferences (auto-check, intervals, skipped version)
UpdateSettingsManager::instance();  // From include/update/UpdateSettingsManager.h

// Beautify feature settings
BeautifySettingsManager::instance();

// Region capture settings
RegionCaptureSettingsManager::instance();

// Display language management
LanguageManager::instance();

// MCP server settings
MCPSettingsManager::instance();

// Other settings
QSettings settings = getSettings();  // From include/settings/Settings.h
```

### Hotkey System

Use `HotkeyManager` as the single entry point for global hotkeys.

- Actions are grouped by category in `HotkeyAction` (`Capture`, `Canvas`, `Clipboard`, `Pin`, `Recording`)
- Do not register `QHotkey` directly in feature code; update via `HotkeyManager`
- Default keys include Region Capture (`F2`) and Screen Canvas (`Ctrl+F2`), with additional configurable actions such as Paste (`F3`), Quick Pin (`Shift+F2`), Pin from Image, Pin History, Hide/Show All Pins, and Record Full Screen

### Tool System

Tools use `ToolId` enum and are registered in `ToolRegistry`. Tool handlers implement `IToolHandler`.

```cpp
// Tool definitions in include/tools/
ToolId.h          // Unified enum (Selection, Pencil, Marker, Arrow, Shape, Text, Mosaic, Crop, Measure, Share, etc.)
ToolDefinition.h  // Tool metadata
IToolHandler.h    // Handler interface (onMousePress, onMouseMove, drawPreview, etc.)
ToolRegistry.h    // Tool registration
ToolManager.h     // Active tool management
```

### Annotation System

Base class: `AnnotationItem` (pure virtual: `draw()`, `boundingRect()`, `clone()`)

Annotation types:
- `PencilStroke`, `MarkerStroke`: Freehand drawing
- `ArrowAnnotation`, `PolylineAnnotation`: Lines and arrows
- `ShapeAnnotation`: Rectangles/ellipses
- `TextBoxAnnotation`: Text with formatting
- `MosaicStroke`, `MosaicRectAnnotation`: Blur/pixelate regions
- `StepBadgeAnnotation`: Numbered badges
- `EmojiStickerAnnotation`: Emoji overlays
- `ErasedItemsGroup`: Groups of erased items
- `AnnotationLayer`: Annotation layer rendering
- `LineStyle`: Shared line style configuration

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

### Platform Abstraction

Use `PlatformFeatures` singleton for capability detection:

```cpp
if (PlatformFeatures::instance().isOCRAvailable()) {
    auto mgr = PlatformFeatures::instance().createOCRManager();
}
```

## Platform-Specific Code

| Feature | Windows | macOS |
|---------|---------|-------|
| Screen Capture | `DXGICaptureEngine_win.cpp` | `SCKCaptureEngine_mac.mm` |
| Video Encoding | `MediaFoundationEncoder.cpp` | `AVFoundationEncoder.mm` |
| Audio Capture | `WASAPIAudioCaptureEngine_win.cpp` | `CoreAudioCaptureEngine_mac.mm` |
| Video Playback | `MediaFoundationPlayer_win.cpp` | `AVFoundationPlayer_mac.mm` |
| Window Detection | `WindowDetector_win.cpp` | `WindowDetector.mm` |
| OCR | `OCRManager_win.cpp` | `OCRManager.mm` |
| Window Level | `WindowLevel_win.cpp` | `WindowLevel_mac.mm` |
| Platform Features | `PlatformFeatures_win.cpp` | `PlatformFeatures_mac.mm` |
| Auto-launch | `AutoLaunchManager_win.cpp` | `AutoLaunchManager_mac.mm` |
| Install Source | `InstallSourceDetector_win.cpp` | `InstallSourceDetector_mac.mm` |
| PATH Utils | `PathEnvUtils_win.cpp` | N/A |
| Color Space | N/A | `ImageColorSpaceHelper_mac.mm` |

## Key Components

### Core Managers (main.cpp + MainApplication)

- `CaptureManager`: Orchestrates region capture workflow
- `PinWindowManager`: Manages floating pin windows
- `ScreenCanvasManager`: Full-screen annotation mode
- `RecordingManager`: Screen recording state machine
- `SingleInstanceGuard`: Prevents multiple instances
- `HotkeyManager`: Centralized hotkey registration and management
- `CLIHandler`: CLI command parsing and IPC dispatch
- `UpdateChecker`: Auto-update version checking

### Region Selector (`src/RegionSelector.cpp`)

Sub-components in `src/region/`:
- `SelectionStateManager`: Selection state tracking
- `MagnifierPanel`: Pixel-level magnification with RGB/HEX preview
- `UpdateThrottler`: Render throttling
- `TextAnnotationEditor`: Inline text editing
- `ShapeAnnotationEditor`: Inline shape annotation editing
- `RegionExportManager`: Export/save functionality
- `RegionInputHandler`: Input event handling
- `RegionPainter`: Region rendering logic
- `MultiRegionManager`: Multi-region capture coordination
- `MultiRegionListPanel`: Multi-region list UI panel
- `RegionToolbarHandler`: Region toolbar interactions
- `RegionSettingsHelper`: Region settings management
- `SelectionDirtyRegionPlanner`: Optimized dirty region calculation
- `SelectionResizeHelper`: Selection resize assistance
- `CaptureShortcutHintsOverlay`: Capture shortcut hints overlay
- `RegionControlWidget`: Region control widget

### Pin Window (`src/PinWindow.cpp`)

Sub-components in `src/pinwindow/`:
- `ResizeHandler`: Edge dragging
- `UIIndicators`: Scale/opacity display
- `PinWindowPlacement`: Positioning and screen-fit behavior
- `RegionLayoutManager`, `RegionLayoutRenderer`: Multi-region layout state and painting
- `PinMergeHelper`: Region merge workflow
- `PinHistoryStore`: Pin history persistence
- `PinHistoryWindow`: Pin history UI

Related toolbar component:
- `WindowedToolbar` (`src/toolbar/WindowedToolbar.cpp`): Pin window toolbar

### Recording (`src/RecordingManager.cpp`)

- `RecordingRegionSelector`: Region selection for recording
- `RecordingControlBar`: Floating control UI
- `RecordingBoundaryOverlay`: Visual boundary
- `RecordingInitTask`: Async capture/encoder initialization
- `RecordingRegionNormalizer`: Region normalization across screens

### CLI System (`src/cli/`)

Commands in `include/cli/commands/`:
- `FullCommand`, `ScreenCommand`, `RegionCommand`: Local capture commands
- `GuiCommand`, `CanvasCommand`, `RecordCommand`, `PinCommand`: IPC commands
- `ConfigCommand`: Settings management

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
- Test suites are organized by component folders under `tests/` (for example `Annotations/`, `Beautify/`, `RecordingManager/`)
- Run `scripts/run-tests.bat` (Windows) or `scripts/run-tests.sh` (macOS) after changes

### Common Patterns to Avoid

- Direct QSettings access (use settings managers)
- Repetitive switch statements (use lookup tables)
- Inline magic numbers (use named constants)
