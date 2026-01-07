cmake_minimum_required(VERSION 3.16...3.31)
project(SnapTray VERSION 1.0.7 LANGUAGES CXX)

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

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Svg Concurrent)

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

# Disable AUTOMOC/AUTOUIC/AUTORCC while fetching OpenCV and libwebp to avoid
# race conditions with generated headers (e.g., OpenCV's opencl_kernels_*.hpp)
set(CMAKE_AUTOMOC OFF)
set(CMAKE_AUTORCC OFF)
set(CMAKE_AUTOUIC OFF)

# ============================================================================
# OpenCV for Scrolling Capture (static build) - Optimized Configuration
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
set(BUILD_opencv_video ON CACHE BOOL "" FORCE)  # Required for optical flow (calcOpticalFlowPyrLK)
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
    src/annotations/EraserStroke.cpp
    src/annotations/TextAnnotation.cpp
    src/settings/AnnotationSettingsManager.cpp
    src/settings/FileSettingsManager.cpp
    src/settings/PinWindowSettingsManager.cpp
    src/utils/CoordinateHelper.cpp
    src/AudioFileWriter.cpp
    # Headers with Q_OBJECT for MOC processing
    include/AudioFileWriter.h
    include/settings/AnnotationSettingsManager.h
    include/settings/FileSettingsManager.h
    include/settings/PinWindowSettingsManager.h
)

target_include_directories(snaptray_core
    PUBLIC
        ${CMAKE_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
        ${CMAKE_BINARY_DIR}
)

target_link_libraries(snaptray_core
    PUBLIC
        Qt6::Core
        Qt6::Gui
        Qt6::Concurrent
)

# Disable qDebug() output in Release builds for core library
target_compile_definitions(snaptray_core PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>
)

# ----------------------------------------------------------------------------
# snaptray_algorithms: Detection and stitching algorithms (OpenCV dependency)
# ----------------------------------------------------------------------------
add_library(snaptray_algorithms STATIC
    # Mosaic annotations (require OpenCV for blur)
    src/annotations/MosaicStroke.cpp
    src/annotations/MosaicRectAnnotation.cpp
    # Detection
    src/detection/FaceDetector.cpp
    src/detection/TextDetector.cpp
    src/detection/AutoBlurManager.cpp
    # Scrolling/Stitching
    src/scrolling/ImageStitcher.cpp
    src/scrolling/ImageStitcherAlgorithms.cpp
    src/scrolling/FixedElementDetector.cpp
    src/stitching/FeatureMatcher.cpp
    src/stitching/OpticalFlowPredictor.cpp
    src/stitching/StaticSceneAnalyzer.cpp
    # Headers with Q_OBJECT for MOC processing
    include/detection/FaceDetector.h
    include/detection/TextDetector.h
    include/detection/AutoBlurManager.h
    include/scrolling/ImageStitcher.h
    include/scrolling/FixedElementDetector.h
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
        ${CMAKE_BINARY_DIR}/_deps/opencv-src/modules/video/include
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
        opencv_video
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
    src/VideoEncoderFactory.cpp
    src/scrolling/AutoScrollController.cpp
    # Headers with Q_OBJECT for MOC processing
    include/IVideoEncoder.h
    include/encoding/NativeGifEncoder.h
    include/encoding/WebPAnimEncoder.h
    include/capture/ICaptureEngine.h
    include/capture/IAudioCaptureEngine.h
    include/capture/QtCaptureEngine.h
    include/video/IVideoPlayer.h

    # macOS sources
    $<$<PLATFORM_ID:Darwin>:
        src/platform/WindowLevel_mac.mm
        src/platform/PlatformFeatures_mac.mm
        src/WindowDetector.mm
        src/OCRManager.mm
        src/AutoLaunchManager_mac.mm
        src/capture/SCKCaptureEngine_mac.mm
        src/capture/CoreAudioCaptureEngine_mac.mm
        src/AVFoundationEncoder.mm
        src/video/AVFoundationPlayer_mac.mm
        src/input/MouseClickTracker_mac.mm
        src/scrolling/AutoScrollController_mac.mm
        src/ColorPickerDialog.mm
    >
    # macOS headers with Q_OBJECT
    $<$<PLATFORM_ID:Darwin>:
        include/AVFoundationEncoder.h
        include/capture/SCKCaptureEngine.h
        include/capture/CoreAudioCaptureEngine.h
        include/ColorPickerDialog.h
        include/input/MouseClickTracker.h
    >

    # Cross-platform headers with Q_OBJECT (need MOC on all platforms)
    include/OCRManager.h
    include/WindowDetector.h

    # Windows sources
    $<$<PLATFORM_ID:Windows>:
        src/platform/WindowLevel_win.cpp
        src/platform/PlatformFeatures_win.cpp
        src/WindowDetector_win.cpp
        src/OCRManager_win.cpp
        src/AutoLaunchManager_win.cpp
        src/capture/DXGICaptureEngine_win.cpp
        src/capture/WASAPIAudioCaptureEngine_win.cpp
        src/MediaFoundationEncoder.cpp
        src/video/MediaFoundationPlayer_win.cpp
        src/input/MouseClickTracker_win.cpp
        src/scrolling/AutoScrollController_win.cpp
        src/ColorPickerDialog_win.cpp
    >
    # Windows headers with Q_OBJECT
    $<$<PLATFORM_ID:Windows>:
        include/MediaFoundationEncoder.h
        include/capture/DXGICaptureEngine.h
        include/capture/WASAPIAudioCaptureEngine.h
        include/ColorPickerDialog.h
        include/input/MouseClickTracker.h
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
)

target_link_libraries(snaptray_platform
    PUBLIC
        snaptray_core
        Qt6::Widgets
        Qt6::Gui
        Qt6::Concurrent
        webp
        libwebpmux
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
    src/IconRenderer.cpp
    src/ToolbarStyle.cpp
    src/ToolbarWidget.cpp
    src/toolbar/ToolbarButtonConfig.cpp
    src/toolbar/ToolbarRenderer.cpp

    # Annotation UI
    src/AnnotationLayer.cpp
    src/AnnotationController.cpp
    src/InlineTextEditor.cpp
    src/TransformationGizmo.cpp

    # Tool system
    src/tools/ToolRegistry.cpp
    src/tools/ToolManager.cpp
    src/tools/handlers/PencilToolHandler.cpp
    src/tools/handlers/MarkerToolHandler.cpp
    src/tools/handlers/ArrowToolHandler.cpp
    src/tools/handlers/PolylineToolHandler.cpp
    src/tools/handlers/ShapeToolHandler.cpp
    src/tools/handlers/MosaicToolHandler.cpp
    src/tools/handlers/StepBadgeToolHandler.cpp
    src/tools/handlers/EmojiStickerToolHandler.cpp
    src/tools/handlers/EraserToolHandler.cpp
    src/tools/handlers/SelectionToolHandler.cpp
    src/tools/handlers/LaserPointerToolHandler.cpp

    # Widgets
    src/ColorPaletteWidget.cpp
    src/ColorAndWidthWidget.cpp
    src/LineWidthWidget.cpp
    src/EmojiPicker.cpp

    # UI sections
    src/ui/sections/ColorSection.cpp
    src/ui/sections/WidthSection.cpp
    src/ui/sections/MosaicWidthSection.cpp
    src/ui/sections/MosaicBlurTypeSection.cpp
    src/ui/sections/TextSection.cpp
    src/ui/sections/ArrowStyleSection.cpp
    src/ui/sections/LineStyleSection.cpp
    src/ui/sections/ShapeSection.cpp
    src/ui/sections/SizeSection.cpp
    src/ui/sections/AutoBlurSection.cpp

    # Region selection
    src/RegionSelector.cpp
    src/region/SelectionStateManager.cpp
    src/region/MagnifierPanel.cpp
    src/region/UpdateThrottler.cpp
    src/region/TextAnnotationEditor.cpp
    src/region/AspectRatioWidget.cpp
    src/region/RadiusSliderWidget.cpp
    src/region/MultiRegionManager.cpp
    src/region/RegionPainter.cpp
    src/region/RegionInputHandler.cpp
    src/region/RegionToolbarHandler.cpp
    src/region/RegionSettingsHelper.cpp
    src/region/SelectionResizeHelper.cpp

    # PinWindow
    src/PinWindow.cpp
    src/PinWindowManager.cpp
    src/pinwindow/ImageTransformer.cpp
    src/pinwindow/ResizeHandler.cpp
    src/pinwindow/UIIndicators.cpp
    src/pinwindow/ClickThroughExitButton.cpp

    # Screen canvas
    src/ScreenCanvas.cpp
    src/ScreenCanvasManager.cpp

    # Renderers
    src/LaserPointerRenderer.cpp
    src/ClickRippleRenderer.cpp
    src/WatermarkRenderer.cpp
    src/LoadingSpinnerRenderer.cpp

    # Recording effects
    src/recording/CursorHighlightEffect.cpp
    src/recording/SpotlightEffect.cpp

    # Video UI
    src/video/VideoPlaybackWidget.cpp
    src/video/VideoTimeline.cpp
    src/video/TrimTimeline.cpp
    src/video/VideoTrimmer.cpp
    src/video/RecordingPreviewWindow.cpp
    src/video/FormatSelectionWidget.cpp
    src/video/InPlacePreviewOverlay.cpp
    src/video/CountdownOverlay.cpp
    src/video/VideoAnnotation.cpp
    src/video/AnnotationTrack.cpp
    src/video/VideoAnnotationRenderer.cpp
    src/video/AnnotationTimelineWidget.cpp
    src/video/VideoAnnotationEditor.cpp

    # Scrolling capture UI
    src/scrolling/ScrollingCaptureManager.cpp
    src/scrolling/ScrollingCaptureOverlay.cpp
    src/scrolling/ScrollingCaptureToolbar.cpp
    src/scrolling/ScrollingCaptureThumbnail.cpp

    # Dialogs and overlays
    src/SettingsDialog.cpp

    # macOS-only UI
    $<$<PLATFORM_ID:Darwin>:
        src/WindowDetectionOverlay.cpp
        src/OCRController.cpp
    >

    # Headers with Q_OBJECT for MOC processing
    include/ToolbarWidget.h
    include/annotations/AnnotationLayer.h
    include/AnnotationController.h
    include/InlineTextEditor.h
    include/TransformationGizmo.h
    include/tools/ToolRegistry.h
    include/tools/ToolManager.h
    include/ColorPaletteWidget.h
    include/ColorAndWidthWidget.h
    include/LineWidthWidget.h
    include/EmojiPicker.h
    include/ui/sections/ColorSection.h
    include/ui/sections/WidthSection.h
    include/ui/sections/MosaicWidthSection.h
    include/ui/sections/MosaicBlurTypeSection.h
    include/ui/sections/TextSection.h
    include/ui/sections/ArrowStyleSection.h
    include/ui/sections/LineStyleSection.h
    include/ui/sections/ShapeSection.h
    include/ui/sections/SizeSection.h
    include/ui/sections/AutoBlurSection.h
    include/RegionSelector.h
    include/region/SelectionStateManager.h
    include/region/MagnifierPanel.h
    include/region/UpdateThrottler.h
    include/region/TextAnnotationEditor.h
    include/region/AspectRatioWidget.h
    include/region/RadiusSliderWidget.h
    include/region/MultiRegionManager.h
    include/region/RegionPainter.h
    include/region/RegionInputHandler.h
    include/region/RegionToolbarHandler.h
    include/region/RegionSettingsHelper.h
    include/PinWindow.h
    include/PinWindowManager.h
    include/pinwindow/ImageTransformer.h
    include/pinwindow/ResizeHandler.h
    include/pinwindow/UIIndicators.h
    include/pinwindow/ClickThroughExitButton.h
    include/ScreenCanvas.h
    include/ScreenCanvasManager.h
    include/LaserPointerRenderer.h
    include/ClickRippleRenderer.h
    include/WatermarkRenderer.h
    include/LoadingSpinnerRenderer.h
    include/recording/CursorHighlightEffect.h
    include/recording/SpotlightEffect.h
    include/video/VideoPlaybackWidget.h
    include/video/VideoTimeline.h
    include/video/TrimTimeline.h
    include/video/VideoTrimmer.h
    include/video/RecordingPreviewWindow.h
    include/video/FormatSelectionWidget.h
    include/video/InPlacePreviewOverlay.h
    include/video/CountdownOverlay.h
    include/video/VideoAnnotation.h
    include/video/AnnotationTrack.h
    include/video/VideoAnnotationRenderer.h
    include/video/AnnotationTimelineWidget.h
    include/video/VideoAnnotationEditor.h
    include/scrolling/ScrollingCaptureManager.h
    include/scrolling/ScrollingCaptureOverlay.h
    include/scrolling/ScrollingCaptureToolbar.h
    include/scrolling/ScrollingCaptureThumbnail.h
    include/SettingsDialog.h
    $<$<PLATFORM_ID:Darwin>:
        include/WindowDetectionOverlay.h
        include/OCRController.h
    >
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
        Qt6::Widgets
        Qt6::Gui
        Qt6::Svg
        QHotkey::QHotkey
)

target_compile_definitions(snaptray_ui PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>
    $<$<CONFIG:Debug>:SNAPTRAY_ENABLE_DEV_FEATURES>
)

# ============================================================================
# Main Application Executable
# ============================================================================

# Application-specific sources (orchestration and entry point)
set(APP_SOURCES
    src/main.cpp
    src/MainApplication.cpp
    src/CaptureManager.cpp
    src/RecordingManager.cpp
    src/RecordingRegionSelector.cpp
    src/RecordingControlBar.cpp
    src/RecordingBoundaryOverlay.cpp
    src/RecordingAnnotationOverlay.cpp
    src/RecordingInitTask.cpp
)

set(RESOURCES
    resources/resources.qrc
)

# Application-specific headers
set(APP_HEADERS
    include/MainApplication.h
    include/CaptureManager.h
    include/RecordingManager.h
    include/RecordingRegionSelector.h
    include/RecordingControlBar.h
    include/RecordingBoundaryOverlay.h
    include/RecordingAnnotationOverlay.h
    include/RecordingInitTask.h
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
    snaptray_ui
    snaptray_platform
    snaptray_algorithms
    snaptray_core
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
# Testing Configuration
# ============================================================================
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
