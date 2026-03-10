cmake_minimum_required(VERSION 3.16...3.31)

# macOS deployment target - MUST be set before project() for proper compiler detection
# Support macOS 14.0+ (matches Homebrew Qt minimum)
if(APPLE)
    set(CMAKE_OSX_DEPLOYMENT_TARGET "14.0" CACHE STRING "Minimum macOS deployment target" FORCE)
endif()

project(SnapTray VERSION 1.0.30 LANGUAGES CXX)

# ============================================================================
# Compiler Cache (ccache/sccache) for faster incremental builds
# ============================================================================
if(NOT DEFINED CMAKE_CXX_COMPILER_LAUNCHER)
    # Try ccache first (macOS/Linux), then sccache (Windows/cross-platform)
    find_program(CCACHE_PROGRAM ccache)
    find_program(SCCACHE_PROGRAM sccache)
    if(CCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        set(CMAKE_C_COMPILER_LAUNCHER "${CCACHE_PROGRAM}")
        message(STATUS "Using ccache: ${CCACHE_PROGRAM}")
    elseif(SCCACHE_PROGRAM)
        set(CMAKE_CXX_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
        set(CMAKE_C_COMPILER_LAUNCHER "${SCCACHE_PROGRAM}")
        message(STATUS "Using sccache: ${SCCACHE_PROGRAM}")
    endif()
endif()

# Standard BUILD_TESTING option
include(CTest)

# ============================================================================
# Version Configuration
# ============================================================================
# Differentiate Debug builds for separate permissions on macOS
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(APP_DISPLAY_NAME "SnapTray-Debug")
    set(APP_BUNDLE_ID "com.victorfu.snaptray.debug")
else()
    set(APP_DISPLAY_NAME "SnapTray")
    set(APP_BUNDLE_ID "com.victorfu.snaptray")
endif()

# Generate version header for C++ code
configure_file(
    "${CMAKE_SOURCE_DIR}/cmake/version.h.in"
    "${CMAKE_BINARY_DIR}/generated/version.h"
    @ONLY
)

# Platform-specific version file generation
if(APPLE)
    # Set variables for Info.plist.in template
    set(MACOSX_BUNDLE_EXECUTABLE_NAME ${PROJECT_NAME})
    set(MACOSX_BUNDLE_BUNDLE_NAME "${APP_DISPLAY_NAME}")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_BUNDLE_ID}")
    set(MACOSX_BUNDLE_ICON_FILE "snaptray.icns")

    # Generate Info.plist from template
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/Info.plist.in"
        "${CMAKE_BINARY_DIR}/Info.plist"
        @ONLY
    )
elseif(WIN32)
    # Generate Windows resource file from template
    configure_file(
        "${CMAKE_SOURCE_DIR}/cmake/snaptray.rc.in"
        "${CMAKE_BINARY_DIR}/snaptray.rc"
        @ONLY
    )
endif()

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# MCP features are only available in Debug builds.
add_compile_definitions($<$<CONFIG:Debug>:SNAPTRAY_ENABLE_MCP>)

# ============================================================================
# MSVC sccache Compatibility: Use /Z7 instead of /Zi for cacheable debug info
# ============================================================================
if(MSVC)
    # Replace /Zi with /Z7 to embed debug info in object files
    # This allows sccache to cache compilations (PDB files are not cacheable)
    string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_DEBUG "${CMAKE_C_FLAGS_DEBUG}")
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG}")
    string(REPLACE "/Zi" "/Z7" CMAKE_C_FLAGS_RELWITHDEBINFO "${CMAKE_C_FLAGS_RELWITHDEBINFO}")
    string(REPLACE "/Zi" "/Z7" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")
endif()

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Svg Concurrent Network LinguistTools Quick QuickControls2 QuickWidgets)

# Enable AUTOMOC for Qt targets (will be disabled for non-Qt dependencies)
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# Fetch QHotkey for global hotkey support (needs AUTOMOC)
include(FetchContent)
FetchContent_Declare(
    QHotkey
    GIT_REPOSITORY https://github.com/Skycoder42/QHotkey.git
    GIT_TAG        master
)
set(QHOTKEY_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(QHotkey)

# ============================================================================
# ZXing-CPP for QR Code / Barcode processing
# ============================================================================
FetchContent_Declare(
    ZXing
    GIT_REPOSITORY https://github.com/zxing-cpp/zxing-cpp.git
    GIT_TAG        v2.2.1
    GIT_SHALLOW    TRUE
)

# Minimal build options for ZXing
set(ZXING_READERS ON CACHE BOOL "" FORCE)   # Enable decoding
set(ZXING_WRITERS ON CACHE BOOL "" FORCE)   # Enable encoding
set(ZXING_USE_BUNDLED_ZINT OFF CACHE BOOL "" FORCE)
set(ZXING_UNIT_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_BLACKBOX_TESTS OFF CACHE BOOL "" FORCE)
set(ZXING_EXAMPLES OFF CACHE BOOL "" FORCE)
set(ZXING_BUILD_PYTHON_MODULE OFF CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(ZXing)

# Fix MSVC UTF-8 encoding issues for ZXing
if(MSVC)
    target_compile_options(ZXing PRIVATE /utf-8)
endif()

# Disable AUTOMOC/AUTOUIC/AUTORCC while fetching OpenCV and libwebp to avoid
# race conditions with generated headers (e.g., OpenCV's opencl_kernels_*.hpp)
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

# ============================================================================
# OpenCV for Detection (static build) - Optimized Configuration
# ============================================================================
# Required modules:
#   - core: Mat operations, basic data structures
#   - imgproc: cvtColor, GaussianBlur, matchTemplate, CLAHE, Sobel
#   - features2d: ORB detector, BFMatcher, MSER (text detection)
#   - flann: Required by features2d for kNN matching
#   - calib3d: Required by objdetect (dependency in OpenCV 4.10.0)
#   - objdetect: CascadeClassifier for Haar Cascade face detection
# ============================================================================
FetchContent_Declare(
    OpenCV
    GIT_REPOSITORY https://github.com/opencv/opencv.git
    GIT_TAG        4.10.0
    GIT_SHALLOW    TRUE
)

# Configure OpenCV for minimal static build (only what we need)
set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
set(BUILD_WITH_STATIC_CRT OFF CACHE BOOL "" FORCE)  # Use dynamic runtime (/MD) to match Qt6
set(BUILD_opencv_apps OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_calib3d ON CACHE BOOL "" FORCE)  # Required by objdetect
set(BUILD_opencv_dnn OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_flann ON CACHE BOOL "" FORCE)  # Required by features2d
set(BUILD_opencv_gapi OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_highgui OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_ml OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_objdetect ON CACHE BOOL "" FORCE)  # Required for face detection (Haar Cascade)
set(BUILD_opencv_photo OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_stitching OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_video OFF CACHE BOOL "" FORCE)
set(BUILD_opencv_videoio OFF CACHE BOOL "" FORCE)
set(BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_PERF_TESTS OFF CACHE BOOL "" FORCE)
set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(WITH_FFMPEG OFF CACHE BOOL "" FORCE)
set(WITH_GSTREAMER OFF CACHE BOOL "" FORCE)
set(WITH_GTK OFF CACHE BOOL "" FORCE)
set(WITH_QT OFF CACHE BOOL "" FORCE)
set(WITH_OPENEXR OFF CACHE BOOL "" FORCE)
set(WITH_JASPER OFF CACHE BOOL "" FORCE)
set(WITH_TIFF OFF CACHE BOOL "" FORCE)
set(WITH_WEBP OFF CACHE BOOL "" FORCE)
set(WITH_PNG OFF CACHE BOOL "" FORCE)
set(WITH_JPEG OFF CACHE BOOL "" FORCE)
set(WITH_OBSENSOR OFF CACHE BOOL "" FORCE)
set(WITH_1394 OFF CACHE BOOL "" FORCE)

# Hardware Acceleration - DISABLED (reduces compile time and binary size)
set(WITH_OPENCL OFF CACHE BOOL "OpenCL: Not needed" FORCE)
set(WITH_OPENCL_SVM OFF CACHE BOOL "OpenCL SVM: Not needed" FORCE)
set(WITH_CUDA OFF CACHE BOOL "CUDA: Not needed" FORCE)
set(WITH_IPP OFF CACHE BOOL "Intel IPP: Reduces binary size ~1-2MB" FORCE)
set(WITH_TBB OFF CACHE BOOL "Intel TBB: Qt provides threading" FORCE)
set(WITH_OPENMP OFF CACHE BOOL "OpenMP: Not needed" FORCE)

# Math Libraries - DISABLED
set(WITH_EIGEN OFF CACHE BOOL "Eigen: Not needed" FORCE)
set(WITH_LAPACK OFF CACHE BOOL "LAPACK: Not needed" FORCE)

# Additional Multimedia - DISABLED
set(WITH_V4L OFF CACHE BOOL "Video4Linux: Not needed" FORCE)
set(WITH_DSHOW OFF CACHE BOOL "DirectShow: Not needed" FORCE)
set(WITH_MSMF OFF CACHE BOOL "Media Foundation via OpenCV: Using directly" FORCE)
set(WITH_AVFOUNDATION OFF CACHE BOOL "AVFoundation via OpenCV: Using directly" FORCE)

# Build Optimizations
set(ENABLE_FAST_MATH ON CACHE BOOL "Enable fast math" FORCE)
set(CV_TRACE OFF CACHE BOOL "Disable tracing" FORCE)
set(CV_ENABLE_INTRINSICS ON CACHE BOOL "Enable SIMD" FORCE)
set(OPENCV_ENABLE_NONFREE OFF CACHE BOOL "Non-free: Not needed" FORCE)

# Language Bindings - DISABLED
set(BUILD_opencv_python2 OFF CACHE BOOL "Python2: Not needed" FORCE)
set(BUILD_opencv_python3 OFF CACHE BOOL "Python3: Not needed" FORCE)
set(BUILD_opencv_java OFF CACHE BOOL "Java: Not needed" FORCE)
set(BUILD_JAVA OFF CACHE BOOL "Java: Not needed" FORCE)
set(BUILD_opencv_world OFF CACHE BOOL "World module: Not needed" FORCE)

# Use system zlib to avoid build conflicts on macOS
set(BUILD_ZLIB OFF CACHE BOOL "" FORCE)
# Keep: core, imgproc, features2d, flann
set(BUILD_opencv_core ON CACHE BOOL "" FORCE)
set(BUILD_opencv_imgproc ON CACHE BOOL "" FORCE)
set(BUILD_opencv_features2d ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(OpenCV)

# ============================================================================
# libwebp for WebP animation encoding
# ============================================================================
FetchContent_Declare(
    libwebp
    GIT_REPOSITORY https://github.com/webmproject/libwebp.git
    GIT_TAG        v1.3.2
    GIT_SHALLOW    TRUE
)
# Disable building command-line tools and extras, but keep libwebpmux for animation
set(WEBP_BUILD_ANIM_UTILS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_CWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_DWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_GIF2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_IMG2WEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_VWEBP OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPINFO OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_WEBPMUX OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_EXTRAS OFF CACHE BOOL "" FORCE)
set(WEBP_BUILD_LIBWEBPMUX ON CACHE BOOL "" FORCE)  # Required for animation encoding
FetchContent_MakeAvailable(libwebp)

# Re-enable AUTOMOC/AUTOUIC/AUTORCC for the main project
set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

# ============================================================================
# Library Targets for Modular Build
# ============================================================================
# Dependency graph:
#   snaptray_app -> snaptray_ui, snaptray_platform, snaptray_algorithms
#   snaptray_ui -> snaptray_core, snaptray_algorithms, snaptray_platform
#   snaptray_platform -> snaptray_core
#   snaptray_algorithms -> snaptray_core
#   snaptray_core -> Qt6 only
# ============================================================================

# ----------------------------------------------------------------------------
# snaptray_core: Foundation layer (annotations, settings, utilities)
# ----------------------------------------------------------------------------
add_library(snaptray_core STATIC
    src/annotations/AnnotationItem.cpp
    src/annotations/PencilStroke.cpp
    src/annotations/MarkerStroke.cpp
    src/annotations/ArrowAnnotation.cpp
    src/annotations/PolylineAnnotation.cpp
    src/annotations/ShapeAnnotation.cpp
    src/annotations/StepBadgeAnnotation.cpp
    src/annotations/EmojiStickerAnnotation.cpp
    src/annotations/ErasedItemsGroup.cpp
    src/annotations/TextBoxAnnotation.cpp
    src/settings/AnnotationSettingsManager.cpp
    src/settings/AutoBlurSettingsManager.cpp
    src/settings/FileSettingsManager.cpp
    src/settings/MCPSettingsManager.cpp
    src/settings/OCRSettingsManager.cpp
    src/settings/PinWindowSettingsManager.cpp
    src/settings/RecordingSettingsManager.cpp
    src/settings/RegionCaptureSettingsManager.cpp
    src/settings/WatermarkSettingsManager.cpp
    src/settings/BeautifySettingsManager.cpp
    src/settings/LanguageManager.cpp
    src/beautify/BeautifyRenderer.cpp
    src/update/UpdateSettingsManager.cpp
    src/cursor/CursorManager.cpp
    src/utils/CoordinateHelper.cpp
    src/utils/DialogThemeUtils.cpp
    src/utils/FilenameTemplateEngine.cpp
    src/utils/ImageSaveUtils.cpp
    # CLI sources
    src/cli/CLIHandler.cpp
    src/cli/IPCProtocol.cpp
    src/cli/CaptureOutputHelper.cpp
    src/cli/commands/GuiCommand.cpp
    src/cli/commands/FullCommand.cpp
    src/cli/commands/ScreenCommand.cpp
    src/cli/commands/RegionCommand.cpp
    src/cli/commands/CanvasCommand.cpp
    src/cli/commands/RecordCommand.cpp
    src/cli/commands/PinCommand.cpp
    src/cli/commands/ConfigCommand.cpp
    # Headers with Q_OBJECT for MOC processing
    include/settings/AnnotationSettingsManager.h
    include/settings/FileSettingsManager.h
    include/settings/MCPSettingsManager.h
    include/settings/PinWindowSettingsManager.h
    include/settings/RegionCaptureSettingsManager.h
    include/settings/SettingsTheme.h
    include/cursor/CursorManager.h
    include/utils/FilenameTemplateEngine.h
    include/utils/ImageSaveUtils.h
)

target_include_directories(snaptray_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_BINARY_DIR}/generated
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}
)

target_link_libraries(snaptray_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Concurrent
        Qt6::Network
)

# Disable qDebug() output in Release builds for core library
target_compile_definitions(snaptray_core PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>
)

# ----------------------------------------------------------------------------
# snaptray_colorwidgets: Color picker widgets library
# ----------------------------------------------------------------------------
add_library(snaptray_colorwidgets STATIC
    src/colorwidgets/ColorUtils.cpp
    src/colorwidgets/ColorWheel.cpp
    src/colorwidgets/GradientSlider.cpp
    src/colorwidgets/HueSlider.cpp
    src/colorwidgets/ColorPreview.cpp
    src/colorwidgets/ColorLineEdit.cpp
    src/colorwidgets/ColorDialog.cpp
    src/colorwidgets/StyledColorDialog.cpp
    src/colorwidgets/ColorPickerDialogCompat.cpp
    # Headers with Q_OBJECT for MOC processing
    include/colorwidgets/ColorWheel.h
    include/colorwidgets/GradientSlider.h
    include/colorwidgets/HueSlider.h
    include/colorwidgets/ColorPreview.h
    include/colorwidgets/ColorLineEdit.h
    include/colorwidgets/ColorDialog.h
    include/colorwidgets/StyledColorDialog.h
    include/colorwidgets/ColorPickerDialogCompat.h
)

target_include_directories(snaptray_colorwidgets
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_BINARY_DIR}
)

target_link_libraries(snaptray_colorwidgets
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
    PRIVATE
        snaptray_platform
)

target_compile_definitions(snaptray_colorwidgets PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>
)

# ----------------------------------------------------------------------------
# snaptray_algorithms: Detection algorithms (OpenCV dependency)
# ----------------------------------------------------------------------------
add_library(snaptray_algorithms STATIC
    # QImage ↔ cv::Mat conversion utility
    src/utils/MatConverter.cpp
    # Mosaic annotations (require OpenCV for blur)
    src/annotations/MosaicStroke.cpp
    src/annotations/MosaicRectAnnotation.cpp
    # Detection
    src/detection/FaceDetector.cpp
    src/detection/AutoBlurManager.cpp
    src/detection/CredentialDetector.cpp
    src/detection/TableDetector.cpp
    # Headers with Q_OBJECT for MOC processing
    include/detection/FaceDetector.h
    include/detection/AutoBlurManager.h
    include/detection/CredentialDetector.h
    include/detection/TableDetector.h
)

target_include_directories(snaptray_algorithms
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_BINARY_DIR}
        ${CMAKE_BINARY_DIR}/_deps/opencv-src/modules/core/include
        ${CMAKE_BINARY_DIR}/_deps/opencv-src/modules/imgproc/include
        ${CMAKE_BINARY_DIR}/_deps/opencv-src/modules/features2d/include
        ${CMAKE_BINARY_DIR}/_deps/opencv-src/modules/flann/include
        ${CMAKE_BINARY_DIR}/_deps/opencv-src/modules/objdetect/include
        ${CMAKE_BINARY_DIR}/_deps/opencv-src/modules/calib3d/include
)

target_link_libraries(snaptray_algorithms
    PUBLIC
        snaptray_core
    PRIVATE
        opencv_core
        opencv_imgproc
        opencv_features2d
        opencv_flann
        opencv_calib3d
        opencv_objdetect
)

target_compile_definitions(snaptray_algorithms PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>
)

# ----------------------------------------------------------------------------
# snaptray_platform: Platform-specific code (capture, encoding, OS APIs)
# ----------------------------------------------------------------------------
add_library(snaptray_platform STATIC
    # Common sources
    src/capture/ICaptureEngine.cpp
    src/capture/IAudioCaptureEngine.cpp
    src/capture/QtCaptureEngine.cpp
    src/encoding/EncoderFactory.cpp
    src/encoding/NativeGifEncoder.cpp
    src/encoding/WebPAnimEncoder.cpp
    src/video/IVideoPlayer.cpp
    src/video/VideoTrimmer.cpp
    src/VideoEncoderFactory.cpp
    src/QRCodeManager.cpp
    $<$<NOT:$<PLATFORM_ID:Darwin>>:
        src/ImageColorSpaceHelper.cpp
    >
    # Headers with Q_OBJECT for MOC processing
    include/IVideoEncoder.h
    include/encoding/NativeGifEncoder.h
    include/encoding/WebPAnimEncoder.h
    include/capture/ICaptureEngine.h
    include/capture/IAudioCaptureEngine.h
    include/capture/QtCaptureEngine.h
    include/video/IVideoPlayer.h
    include/video/VideoTrimmer.h
    include/QRCodeManager.h
    include/ImageColorSpaceHelper.h

    # macOS sources
    $<$<PLATFORM_ID:Darwin>:
        src/ImageColorSpaceHelper_mac.mm
        src/platform/WindowLevel_mac.mm
        src/platform/PlatformFeatures_mac.mm
        src/WindowDetector.mm
        src/OCRManager.mm
        src/AutoLaunchManager_mac.mm
        src/capture/SCKCaptureEngine_mac.mm
        src/capture/CoreAudioCaptureEngine_mac.mm
        src/AVFoundationEncoder.mm
        src/video/AVFoundationPlayer_mac.mm
    >
    # macOS headers with Q_OBJECT
    $<$<PLATFORM_ID:Darwin>:
        include/AVFoundationEncoder.h
        include/capture/SCKCaptureEngine.h
        include/capture/CoreAudioCaptureEngine.h
    >

    # Cross-platform headers with Q_OBJECT (need MOC on all platforms)
    include/OCRManager.h
    include/WindowDetector.h

    # Windows sources
    $<$<PLATFORM_ID:Windows>:
        src/platform/WindowLevel_win.cpp
        src/platform/PathEnvUtils_win.cpp
        src/platform/PlatformFeatures_win.cpp
        src/WindowDetector_win.cpp
        src/OCRManager_win.cpp
        src/AutoLaunchManager_win.cpp
        src/capture/DXGICaptureEngine_win.cpp
        src/capture/WASAPIAudioCaptureEngine_win.cpp
        src/MediaFoundationEncoder.cpp
        src/video/MediaFoundationPlayer_win.cpp
    >
    # Windows headers with Q_OBJECT
    $<$<PLATFORM_ID:Windows>:
        include/MediaFoundationEncoder.h
        include/capture/DXGICaptureEngine.h
        include/capture/WASAPIAudioCaptureEngine.h
    >
)

# Enable ARC for AVFoundationPlayer (uses __weak references)
set_source_files_properties(
    src/video/AVFoundationPlayer_mac.mm
    PROPERTIES COMPILE_FLAGS "-fobjc-arc"
)

target_include_directories(snaptray_platform
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_BINARY_DIR}
        ${CMAKE_BINARY_DIR}/_deps/libwebp-src/src
        ${CMAKE_BINARY_DIR}/_deps/zxing-src/core/src
        ${CMAKE_BINARY_DIR}/_deps/zxing-build/core
)

target_link_libraries(snaptray_platform
    PUBLIC
        snaptray_core
        Qt6::Widgets
        Qt6::Gui
        Qt6::Concurrent
        webp
        libwebpmux
        ZXing::ZXing
    PRIVATE
        $<$<PLATFORM_ID:Darwin>:
            "-framework CoreGraphics"
            "-framework ApplicationServices"
            "-framework AppKit"
            "-framework Vision"
            "-framework ServiceManagement"
            "-framework CoreMedia"
            "-framework CoreVideo"
            "-framework AVFoundation"
            "-framework CoreAudio"
            "-framework AudioToolbox"
            "-weak_framework ScreenCaptureKit"
        >
        $<$<PLATFORM_ID:Windows>:
            dwmapi
            psapi
            windowsapp
            d3d11
            dxgi
            mfreadwrite
            mfplat
            mfuuid
            ole32
            propsys
        >
)

# Windows-specific compiler flags
if(WIN32)
    target_compile_options(snaptray_platform PRIVATE /await)
endif()

target_compile_definitions(snaptray_platform PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>
)

# Suppress duplicate library warnings on macOS
if(APPLE)
    target_link_options(snaptray_platform PRIVATE "-Wl,-no_warn_duplicate_libraries")
endif()

# ----------------------------------------------------------------------------
# snaptray_ui: UI components (widgets, tools, renderers)
# ----------------------------------------------------------------------------
add_library(snaptray_ui STATIC
    # Rendering
    src/GlassRenderer.cpp
    src/GlassPanelCSS.cpp
    src/IconRenderer.cpp
    src/ToolbarStyle.cpp
    # Annotation UI
    src/AnnotationLayer.cpp
    src/InlineTextEditor.cpp
    src/TransformationGizmo.cpp
    src/annotation/AnnotationContext.cpp

    # Tool system
    src/tools/ToolRegistry.cpp
    src/tools/ToolManager.cpp
    src/tools/ToolSectionConfig.cpp
    src/tools/handlers/PencilToolHandler.cpp
    src/tools/handlers/MarkerToolHandler.cpp
    src/tools/handlers/ArrowToolHandler.cpp
    src/tools/handlers/PolylineToolHandler.cpp
    src/tools/handlers/ShapeToolHandler.cpp
    src/tools/handlers/MosaicToolHandler.cpp
    src/tools/handlers/EraserToolHandler.cpp
    src/tools/handlers/StepBadgeToolHandler.cpp
    src/tools/handlers/EmojiStickerToolHandler.cpp
    src/tools/handlers/TextToolHandler.cpp
    src/tools/handlers/SelectionToolHandler.cpp
    src/tools/handlers/CropToolHandler.cpp
    src/tools/handlers/MeasureToolHandler.cpp

    # Widgets
    src/EmojiPicker.cpp
    src/pinwindow/EmojiPickerPopup.cpp

    # Hotkey management system
    src/hotkey/HotkeyManager.cpp
    src/widgets/TypeHotkeyDialog.cpp
    src/widgets/HotkeySettingsTab.cpp
    include/hotkey/HotkeyManager.h
    include/widgets/TypeHotkeyDialog.h
    include/widgets/HotkeySettingsTab.h

    # Region selection
    src/RegionSelector.cpp
    src/region/SelectionStateManager.cpp
    src/region/MagnifierPanel.cpp
    src/region/UpdateThrottler.cpp
    src/region/TextAnnotationEditor.cpp
    src/region/ShapeAnnotationEditor.cpp
    src/region/RegionControlWidget.cpp
    src/region/MultiRegionManager.cpp
    src/region/MultiRegionListPanel.cpp
    src/region/RegionPainter.cpp
    src/region/RegionInputHandler.cpp
    src/region/SelectionDirtyRegionPlanner.cpp
    src/region/RegionToolbarHandler.cpp
    src/region/RegionSettingsHelper.cpp
    src/region/SelectionResizeHelper.cpp
    src/region/RegionExportManager.cpp
    src/region/CaptureShortcutHintsOverlay.cpp

    # PinWindow
    src/PinWindow.cpp
    src/PinWindowManager.cpp
    src/pinwindow/PinHistoryStore.cpp
    src/pinwindow/PinHistoryWindow.cpp
    src/pinwindow/PinWindowPlacement.cpp
    src/pinwindow/ResizeHandler.cpp
    src/pinwindow/UIIndicators.cpp
    src/pinwindow/ClickThroughExitButton.cpp
    src/pinwindow/RegionLayoutManager.cpp
    src/pinwindow/RegionLayoutRenderer.cpp
    src/pinwindow/PinMergeHelper.cpp

    # Screen canvas
    src/ScreenCanvas.cpp
    src/ScreenCanvasManager.cpp

    # Renderers
    src/LaserPointerRenderer.cpp
    src/WatermarkRenderer.cpp
    src/LoadingSpinnerRenderer.cpp

    # Beautify
    src/beautify/BeautifyPanel.cpp
    include/beautify/BeautifyPanel.h

    # Share
    src/share/ShareUploadClient.cpp

    # Global UI components
    src/ui/DesignTokens.cpp
    src/ui/GlassTooltip.cpp
    include/ui/DesignTokens.h
    include/ui/GlassTooltip.h
    include/share/ShareUploadClient.h

    # Update system
    src/update/InstallSourceDetector.cpp
    src/update/UpdateChecker.cpp
    include/update/InstallSourceDetector.h
    include/update/UpdateChecker.h
    include/update/UpdateSettingsManager.h

    # Platform-specific update sources
    $<$<PLATFORM_ID:Darwin>:
        src/update/InstallSourceDetector_mac.mm
    >
    $<$<PLATFORM_ID:Windows>:
        src/update/InstallSourceDetector_win.cpp
    >

    # macOS-only UI

    # Headers with Q_OBJECT for MOC processing
    include/annotations/AnnotationLayer.h
    include/InlineTextEditor.h
    include/TransformationGizmo.h
    include/tools/ToolRegistry.h
    include/tools/ToolManager.h
    include/EmojiPicker.h
    include/pinwindow/EmojiPickerPopup.h
    include/RegionSelector.h
    include/region/SelectionStateManager.h
    include/region/MagnifierPanel.h
    include/region/UpdateThrottler.h
    include/region/TextAnnotationEditor.h
    include/region/ShapeAnnotationEditor.h
    include/region/RegionControlWidget.h
    include/region/MultiRegionManager.h
    include/region/MultiRegionListPanel.h
    include/region/RegionPainter.h
    include/region/RegionInputHandler.h
    include/region/SelectionDirtyRegionPlanner.h
    include/region/RegionToolbarHandler.h
    include/region/RegionSettingsHelper.h
    include/region/RegionExportManager.h
    include/region/CaptureShortcutHintsOverlay.h
    include/PinWindow.h
    include/PinWindowManager.h
    include/pinwindow/PinHistoryStore.h
    include/pinwindow/PinHistoryWindow.h
    include/pinwindow/PinWindowPlacement.h
    include/pinwindow/ResizeHandler.h
    include/pinwindow/UIIndicators.h
    include/pinwindow/ClickThroughExitButton.h
    include/pinwindow/RegionLayoutManager.h
    include/pinwindow/PinMergeHelper.h
    include/ScreenCanvas.h
    include/ScreenCanvasManager.h
    include/LaserPointerRenderer.h
    include/WatermarkRenderer.h
    include/LoadingSpinnerRenderer.h

    # Recording management (moved from APP_SOURCES for testability)
    src/RecordingManager.cpp
    src/RecordingInitTask.cpp
    src/RecordingRegionNormalizer.cpp
    src/encoding/EncodingWorker.cpp
    include/RecordingManager.h
    include/RecordingInitTask.h
    include/RecordingRegionNormalizer.h
    include/encoding/EncodingWorker.h
)

target_include_directories(snaptray_ui
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_BINARY_DIR}
)

target_link_libraries(snaptray_ui
    PUBLIC
        snaptray_core
        snaptray_algorithms
        snaptray_platform
        snaptray_colorwidgets
        snaptray_qml
        Qt6::Widgets
        Qt6::Gui
        Qt6::Svg
        QHotkey::QHotkey
)

target_compile_definitions(snaptray_ui PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>
    $<$<CONFIG:Debug>:SNAPTRAY_ENABLE_DEV_FEATURES>
)

# ----------------------------------------------------------------------------
# snaptray_mcp: MCP HTTP server and tool handlers
# ----------------------------------------------------------------------------
add_library(snaptray_mcp STATIC
    src/mcp/MCPServer.cpp
    src/mcp/MCPHttpTransport.cpp
    src/mcp/MCPTools.cpp
    src/mcp/MCPScreenCaptureTool.cpp
    src/mcp/MCPPinTool.cpp
    src/mcp/MCPShareTool.cpp
    include/mcp/MCPServer.h
    include/mcp/MCPHttpTransport.h
    include/mcp/MCPTools.h
)

target_include_directories(snaptray_mcp
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_BINARY_DIR}
)

target_link_libraries(snaptray_mcp
    PUBLIC
        snaptray_ui
        Qt6::Network
        Qt6::Gui
        Qt6::Widgets
)

target_compile_definitions(snaptray_mcp PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>
)

# ----------------------------------------------------------------------------
# snaptray_qml: QML/Qt Quick module (annotation canvas, QML UI)
# ----------------------------------------------------------------------------
# Token singletons: flatten into module root so they are accessible as
# PrimitiveTokens / SemanticTokens / ComponentTokens within SnapTrayQml.
# Token singletons
set_source_files_properties(
    src/qml/tokens/PrimitiveTokens.qml
    src/qml/tokens/SemanticTokens.qml
    src/qml/tokens/ComponentTokens.qml
    PROPERTIES
        QT_QML_SINGLETON_TYPE TRUE
)

# Flatten resource aliases so QML files are at e.g. :/SnapTrayQml/components/Toast.qml
# instead of :/SnapTrayQml/src/qml/components/Toast.qml
set_source_files_properties(src/qml/main.qml PROPERTIES QT_RESOURCE_ALIAS main.qml)
set_source_files_properties(src/qml/tokens/PrimitiveTokens.qml PROPERTIES QT_RESOURCE_ALIAS tokens/PrimitiveTokens.qml)
set_source_files_properties(src/qml/tokens/SemanticTokens.qml PROPERTIES QT_RESOURCE_ALIAS tokens/SemanticTokens.qml)
set_source_files_properties(src/qml/tokens/ComponentTokens.qml PROPERTIES QT_RESOURCE_ALIAS tokens/ComponentTokens.qml)
set_source_files_properties(src/qml/components/Toast.qml PROPERTIES QT_RESOURCE_ALIAS components/Toast.qml)
set_source_files_properties(src/qml/components/Badge.qml PROPERTIES QT_RESOURCE_ALIAS components/Badge.qml)
set_source_files_properties(src/qml/components/RecordingBoundary.qml PROPERTIES QT_RESOURCE_ALIAS components/RecordingBoundary.qml)
set_source_files_properties(src/qml/components/Countdown.qml PROPERTIES QT_RESOURCE_ALIAS components/Countdown.qml)
set_source_files_properties(src/qml/components/RecordingTooltip.qml PROPERTIES QT_RESOURCE_ALIAS components/RecordingTooltip.qml)
set_source_files_properties(src/qml/components/GlassSurface.qml PROPERTIES QT_RESOURCE_ALIAS components/GlassSurface.qml)
set_source_files_properties(src/qml/components/ToolbarButton.qml PROPERTIES QT_RESOURCE_ALIAS components/ToolbarButton.qml)

# Floating toolbar
set_source_files_properties(src/qml/toolbar/FloatingToolbar.qml PROPERTIES QT_RESOURCE_ALIAS toolbar/FloatingToolbar.qml)
set_source_files_properties(src/qml/toolbar/ToolOptionsStrip.qml PROPERTIES QT_RESOURCE_ALIAS toolbar/ToolOptionsStrip.qml)
set_source_files_properties(src/qml/toolbar/ColorPaletteSection.qml PROPERTIES QT_RESOURCE_ALIAS toolbar/ColorPaletteSection.qml)
set_source_files_properties(src/qml/toolbar/WidthSection.qml PROPERTIES QT_RESOURCE_ALIAS toolbar/WidthSection.qml)
set_source_files_properties(src/qml/toolbar/TextFormattingSection.qml PROPERTIES QT_RESOURCE_ALIAS toolbar/TextFormattingSection.qml)
set_source_files_properties(src/qml/toolbar/ShapeSection.qml PROPERTIES QT_RESOURCE_ALIAS toolbar/ShapeSection.qml)
set_source_files_properties(src/qml/toolbar/ArrowStyleSection.qml PROPERTIES QT_RESOURCE_ALIAS toolbar/ArrowStyleSection.qml)
set_source_files_properties(src/qml/toolbar/LineStyleSection.qml PROPERTIES QT_RESOURCE_ALIAS toolbar/LineStyleSection.qml)
set_source_files_properties(src/qml/toolbar/SizeSection.qml PROPERTIES QT_RESOURCE_ALIAS toolbar/SizeSection.qml)
set_source_files_properties(src/qml/toolbar/AutoBlurSection.qml PROPERTIES QT_RESOURCE_ALIAS toolbar/AutoBlurSection.qml)

# Settings controls
set_source_files_properties(src/qml/controls/SettingsSection.qml PROPERTIES QT_RESOURCE_ALIAS controls/SettingsSection.qml)
set_source_files_properties(src/qml/controls/SettingsRow.qml PROPERTIES QT_RESOURCE_ALIAS controls/SettingsRow.qml)
set_source_files_properties(src/qml/controls/SettingsToggle.qml PROPERTIES QT_RESOURCE_ALIAS controls/SettingsToggle.qml)
set_source_files_properties(src/qml/controls/SettingsSlider.qml PROPERTIES QT_RESOURCE_ALIAS controls/SettingsSlider.qml)
set_source_files_properties(src/qml/controls/SettingsCombo.qml PROPERTIES QT_RESOURCE_ALIAS controls/SettingsCombo.qml)
set_source_files_properties(src/qml/controls/SettingsPathPicker.qml PROPERTIES QT_RESOURCE_ALIAS controls/SettingsPathPicker.qml)
set_source_files_properties(src/qml/controls/SettingsButton.qml PROPERTIES QT_RESOURCE_ALIAS controls/SettingsButton.qml)
set_source_files_properties(src/qml/controls/SettingsPermissionRow.qml PROPERTIES QT_RESOURCE_ALIAS controls/SettingsPermissionRow.qml)
set_source_files_properties(src/qml/controls/Spacer.qml PROPERTIES QT_RESOURCE_ALIAS controls/Spacer.qml)
set_source_files_properties(src/qml/controls/FocusFrame.qml PROPERTIES QT_RESOURCE_ALIAS controls/FocusFrame.qml)
set_source_files_properties(src/qml/controls/BusySpinner.qml PROPERTIES QT_RESOURCE_ALIAS controls/BusySpinner.qml)
set_source_files_properties(src/qml/controls/SettingsRadioGroup.qml PROPERTIES QT_RESOURCE_ALIAS controls/SettingsRadioGroup.qml)
set_source_files_properties(src/qml/controls/SvgIcon.qml PROPERTIES QT_RESOURCE_ALIAS controls/SvgIcon.qml)

# Settings pages
set_source_files_properties(src/qml/settings/SettingsWindow.qml PROPERTIES QT_RESOURCE_ALIAS settings/SettingsWindow.qml)
set_source_files_properties(src/qml/settings/SettingsSidebar.qml PROPERTIES QT_RESOURCE_ALIAS settings/SettingsSidebar.qml)
set_source_files_properties(src/qml/settings/GeneralSettings.qml PROPERTIES QT_RESOURCE_ALIAS settings/GeneralSettings.qml)
set_source_files_properties(src/qml/settings/HotkeySettings.qml PROPERTIES QT_RESOURCE_ALIAS settings/HotkeySettings.qml)
set_source_files_properties(src/qml/settings/AdvancedSettings.qml PROPERTIES QT_RESOURCE_ALIAS settings/AdvancedSettings.qml)
set_source_files_properties(src/qml/settings/WatermarkSettings.qml PROPERTIES QT_RESOURCE_ALIAS settings/WatermarkSettings.qml)
set_source_files_properties(src/qml/settings/OCRSettings.qml PROPERTIES QT_RESOURCE_ALIAS settings/OCRSettings.qml)
set_source_files_properties(src/qml/settings/RecordingSettings.qml PROPERTIES QT_RESOURCE_ALIAS settings/RecordingSettings.qml)
set_source_files_properties(src/qml/settings/FilesSettings.qml PROPERTIES QT_RESOURCE_ALIAS settings/FilesSettings.qml)
set_source_files_properties(src/qml/settings/UpdatesSettings.qml PROPERTIES QT_RESOURCE_ALIAS settings/UpdatesSettings.qml)
set_source_files_properties(src/qml/settings/AboutSettings.qml PROPERTIES QT_RESOURCE_ALIAS settings/AboutSettings.qml)

# Dialog components
set_source_files_properties(src/qml/components/DialogBase.qml PROPERTIES QT_RESOURCE_ALIAS components/DialogBase.qml)
set_source_files_properties(src/qml/controls/DialogButton.qml PROPERTIES QT_RESOURCE_ALIAS controls/DialogButton.qml)
set_source_files_properties(src/qml/dialogs/SharePasswordDialog.qml PROPERTIES QT_RESOURCE_ALIAS dialogs/SharePasswordDialog.qml)
set_source_files_properties(src/qml/dialogs/ShareResultDialog.qml PROPERTIES QT_RESOURCE_ALIAS dialogs/ShareResultDialog.qml)
set_source_files_properties(src/qml/dialogs/OCRResultDialog.qml PROPERTIES QT_RESOURCE_ALIAS dialogs/OCRResultDialog.qml)
set_source_files_properties(src/qml/dialogs/UpdateDialog.qml PROPERTIES QT_RESOURCE_ALIAS dialogs/UpdateDialog.qml)
set_source_files_properties(src/qml/dialogs/QRCodeResultDialog.qml PROPERTIES QT_RESOURCE_ALIAS dialogs/QRCodeResultDialog.qml)

# Recording preview
set_source_files_properties(src/qml/recording/RecordingPreview.qml PROPERTIES QT_RESOURCE_ALIAS recording/RecordingPreview.qml)

# Recording control bar
set_source_files_properties(src/qml/recording/RecordingControlBar.qml PROPERTIES QT_RESOURCE_ALIAS recording/RecordingControlBar.qml)
set_source_files_properties(src/qml/recording/RecordingRegionSelector.qml PROPERTIES QT_RESOURCE_ALIAS recording/RecordingRegionSelector.qml)

qt_add_qml_module(snaptray_qml
    URI SnapTrayQml
    VERSION 1.0
    STATIC
    QML_FILES
        src/qml/main.qml
        src/qml/tokens/PrimitiveTokens.qml
        src/qml/tokens/SemanticTokens.qml
        src/qml/tokens/ComponentTokens.qml
        src/qml/components/Toast.qml
        src/qml/components/Badge.qml
        src/qml/components/RecordingBoundary.qml
        src/qml/components/Countdown.qml
        src/qml/components/RecordingTooltip.qml
        src/qml/components/GlassSurface.qml
        src/qml/components/ToolbarButton.qml
        src/qml/components/ToolbarChevron.qml
        src/qml/components/DialogBase.qml
        src/qml/controls/SettingsSection.qml
        src/qml/controls/SettingsRow.qml
        src/qml/controls/SettingsToggle.qml
        src/qml/controls/SettingsSlider.qml
        src/qml/controls/SettingsCombo.qml
        src/qml/controls/SettingsPathPicker.qml
        src/qml/controls/SettingsButton.qml
        src/qml/controls/SettingsPermissionRow.qml
        src/qml/controls/Spacer.qml
        src/qml/controls/FocusFrame.qml
        src/qml/controls/BusySpinner.qml
        src/qml/controls/SettingsRadioGroup.qml
        src/qml/controls/SvgIcon.qml
        src/qml/controls/DialogButton.qml
        src/qml/settings/SettingsWindow.qml
        src/qml/settings/SettingsSidebar.qml
        src/qml/settings/GeneralSettings.qml
        src/qml/settings/HotkeySettings.qml
        src/qml/settings/AdvancedSettings.qml
        src/qml/settings/WatermarkSettings.qml
        src/qml/settings/OCRSettings.qml
        src/qml/settings/RecordingSettings.qml
        src/qml/settings/FilesSettings.qml
        src/qml/settings/UpdatesSettings.qml
        src/qml/settings/AboutSettings.qml
        src/qml/recording/RecordingPreview.qml
        src/qml/recording/RecordingControlBar.qml
        src/qml/recording/RecordingRegionSelector.qml
        src/qml/dialogs/SharePasswordDialog.qml
        src/qml/dialogs/ShareResultDialog.qml
        src/qml/dialogs/OCRResultDialog.qml
        src/qml/dialogs/UpdateDialog.qml
        src/qml/dialogs/QRCodeResultDialog.qml
        src/qml/toolbar/FloatingToolbar.qml
        src/qml/toolbar/ToolOptionsStrip.qml
        src/qml/toolbar/ColorPaletteSection.qml
        src/qml/toolbar/WidthSection.qml
        src/qml/toolbar/TextFormattingSection.qml
        src/qml/toolbar/ShapeSection.qml
        src/qml/toolbar/ArrowStyleSection.qml
        src/qml/toolbar/LineStyleSection.qml
        src/qml/toolbar/SizeSection.qml
        src/qml/toolbar/AutoBlurSection.qml
    SOURCES
        src/ui/DesignSystem.cpp
        include/ui/DesignSystem.h
        src/qml/AnnotationCanvas.cpp
        include/qml/AnnotationCanvas.h
        src/qml/ThemeManager.cpp
        include/qml/ThemeManager.h
        src/qml/QmlOverlayManager.mm
        include/qml/QmlOverlayManager.h
        src/qml/QmlToast.cpp
        include/qml/QmlToast.h
        src/qml/QmlBadge.cpp
        include/qml/QmlBadge.h
        src/qml/QmlRecordingBoundary.mm
        include/qml/QmlRecordingBoundary.h
        src/qml/QmlRecordingControlBar.mm
        include/qml/QmlRecordingControlBar.h
        src/qml/QmlRecordingRegionSelector.mm
        include/qml/QmlRecordingRegionSelector.h
        src/qml/QmlCountdownOverlay.mm
        include/qml/QmlCountdownOverlay.h
        src/qml/VideoPlaybackItem.cpp
        include/qml/VideoPlaybackItem.h
        src/qml/SvgIconItem.cpp
        include/qml/SvgIconItem.h
        src/qml/RecordingPreviewBackend.mm
        include/qml/RecordingPreviewBackend.h
        src/qml/ToolbarViewModelBase.cpp
        include/qml/ToolbarViewModelBase.h
        src/qml/PinToolbarViewModel.cpp
        include/qml/PinToolbarViewModel.h
        src/qml/QmlWindowedToolbar.mm
        include/qml/QmlWindowedToolbar.h
        src/qml/PinToolOptionsViewModel.cpp
        include/qml/PinToolOptionsViewModel.h
        src/qml/RegionToolbarViewModel.cpp
        include/qml/RegionToolbarViewModel.h
        src/qml/CanvasToolbarViewModel.cpp
        include/qml/CanvasToolbarViewModel.h
        src/qml/QmlFloatingToolbar.mm
        include/qml/QmlFloatingToolbar.h
        src/qml/QmlFloatingSubToolbar.mm
        include/qml/QmlFloatingSubToolbar.h
        src/qml/QmlDialog.mm
        include/qml/QmlDialog.h
        src/qml/DialogImageProvider.cpp
        include/qml/DialogImageProvider.h
        src/qml/SharePasswordViewModel.cpp
        include/qml/SharePasswordViewModel.h
        src/qml/ShareResultViewModel.cpp
        include/qml/ShareResultViewModel.h
        src/qml/OCRResultViewModel.cpp
        include/qml/OCRResultViewModel.h
        src/qml/UpdateDialogViewModel.cpp
        include/qml/UpdateDialogViewModel.h
        src/qml/QRCodeResultViewModel.cpp
        include/qml/QRCodeResultViewModel.h
)

target_include_directories(snaptray_qml
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/include/qml
        ${CMAKE_SOURCE_DIR}/include/ui
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_BINARY_DIR}
)

target_link_libraries(snaptray_qml
    PUBLIC
        snaptray_core
        snaptray_platform
        Qt6::Quick
        Qt6::QuickControls2
        Qt6::QuickWidgets
        Qt6::Svg
        Qt6::Concurrent
)

if(APPLE)
    target_link_libraries(snaptray_qml PRIVATE "-framework Cocoa")
endif()

target_compile_definitions(snaptray_qml PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>
)

# ============================================================================
# Main Application Executable
# ============================================================================

# Application-specific sources (orchestration and entry point)
# NOTE: Recording components moved to snaptray_ui for testability
set(APP_SOURCES
    src/main.cpp
    src/MainApplication.cpp
    src/CaptureManager.cpp
    src/SingleInstanceGuard.cpp
    src/qml/SettingsBackend.cpp
    src/qml/QmlSettingsWindow.cpp
)

set(RESOURCES
    resources/resources.qrc
)

# Application-specific headers
set(APP_HEADERS
    include/MainApplication.h
    include/CaptureManager.h
    include/SingleInstanceGuard.h
    include/qml/SettingsBackend.h
    include/qml/QmlSettingsWindow.h
)

# Windows application icon resource
if(WIN32)
    set(WIN32_RESOURCES "${CMAKE_BINARY_DIR}/snaptray.rc")
endif()

# Create executable linking to library targets
add_executable(${PROJECT_NAME} ${APP_SOURCES} ${APP_HEADERS} ${RESOURCES} ${WIN32_RESOURCES})

# Only use Windows subsystem (no console) for non-Debug builds
# Debug builds will show a console window for easier debugging
if(WIN32)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        WIN32_EXECUTABLE $<NOT:$<CONFIG:Debug>>
        OUTPUT_NAME "${APP_DISPLAY_NAME}"
    )
endif()

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
    ${CMAKE_BINARY_DIR}
)

# Link all library targets - they handle their own dependencies
target_link_libraries(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:snaptray_mcp>
    snaptray_ui
    snaptray_platform
    snaptray_algorithms
    snaptray_core
    snaptray_qmlplugin
    Qt6::Network
)

# Disable qDebug() output in Release builds
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>
)

# Enable development features only in Debug builds
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:SNAPTRAY_ENABLE_DEV_FEATURES>
)

# macOS-specific settings
if(APPLE)
    # Suppress duplicate library warnings from OpenCV dependencies
    target_link_options(${PROJECT_NAME} PRIVATE "-Wl,-no_warn_duplicate_libraries")
    # Add icon to bundle
    set(MACOSX_BUNDLE_ICON_FILE snaptray.icns)
    set(APP_ICON_MACOSX ${CMAKE_SOURCE_DIR}/resources/icons/snaptray.icns)
    set_source_files_properties(${APP_ICON_MACOSX} PROPERTIES
        MACOSX_PACKAGE_LOCATION "Resources"
    )
    target_sources(${PROJECT_NAME} PRIVATE ${APP_ICON_MACOSX})

    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_BINARY_DIR}/Info.plist"
        MACOSX_BUNDLE_BUNDLE_NAME "${APP_DISPLAY_NAME}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "${APP_BUNDLE_ID}"
        MACOSX_BUNDLE_ICON_FILE snaptray.icns
        OUTPUT_NAME "${APP_DISPLAY_NAME}"
    )
endif()

# ============================================================================
# Internationalization (i18n)
# ============================================================================
set(TS_FILES
    translations/snaptray_ar.ts
    translations/snaptray_cs.ts
    translations/snaptray_de.ts
    translations/snaptray_el.ts
    translations/snaptray_es_MX.ts
    translations/snaptray_fi.ts
    translations/snaptray_fr.ts
    translations/snaptray_it.ts
    translations/snaptray_ja.ts
    translations/snaptray_ko.ts
    translations/snaptray_lt.ts
    translations/snaptray_nl.ts
    translations/snaptray_pl.ts
    translations/snaptray_pt.ts
    translations/snaptray_pt_PT.ts
    translations/snaptray_ru.ts
    translations/snaptray_sr.ts
    translations/snaptray_sv.ts
    translations/snaptray_tr.ts
    translations/snaptray_vi.ts
    translations/snaptray_zh_CN.ts
    translations/snaptray_zh_HK.ts
    translations/snaptray_zh_TW.ts
)

qt_add_translations(${PROJECT_NAME}
    TS_FILES ${TS_FILES}
    RESOURCE_PREFIX "/i18n"
    SOURCE_TARGETS
        snaptray_qml
        snaptray_core
        snaptray_ui
        snaptray_qml
        snaptray_platform
        snaptray_colorwidgets
        snaptray_algorithms
        ${PROJECT_NAME}
)

# ============================================================================
# Testing Configuration
# ============================================================================
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
