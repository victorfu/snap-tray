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

set(SOURCES
    src/main.cpp
    src/MainApplication.cpp
    src/SettingsDialog.cpp
    src/RegionSelector.cpp
    src/PinWindow.cpp
    src/PinWindowManager.cpp
    src/AnnotationLayer.cpp
    src/annotations/AnnotationItem.cpp
    src/annotations/PencilStroke.cpp
    src/annotations/MarkerStroke.cpp
    src/annotations/ArrowAnnotation.cpp
    src/annotations/PolylineAnnotation.cpp
    src/annotations/ShapeAnnotation.cpp
    src/annotations/MosaicStroke.cpp
    src/annotations/MosaicRectAnnotation.cpp
    src/annotations/StepBadgeAnnotation.cpp
    src/annotations/EmojiStickerAnnotation.cpp
    src/annotations/ErasedItemsGroup.cpp
    src/annotations/EraserStroke.cpp
    src/annotations/TextAnnotation.cpp
    src/ScreenCanvas.cpp
    src/IconRenderer.cpp
    src/ColorPaletteWidget.cpp
    src/EmojiPicker.cpp
    src/LineWidthWidget.cpp
    src/ColorAndWidthWidget.cpp
    src/ToolbarWidget.cpp
    src/ToolbarStyle.cpp
    src/GlassRenderer.cpp
    src/toolbar/ToolbarButtonConfig.cpp
    src/toolbar/ToolbarRenderer.cpp
    src/settings/AnnotationSettingsManager.cpp
    src/settings/FileSettingsManager.cpp
    src/settings/PinWindowSettingsManager.cpp
    src/AnnotationController.cpp
    src/ColorPickerDialog.mm
    src/InlineTextEditor.cpp
    src/LaserPointerRenderer.cpp
    src/ClickRippleRenderer.cpp
    src/WatermarkRenderer.cpp
    src/LoadingSpinnerRenderer.cpp
    src/RecordingManager.cpp
    src/RecordingRegionSelector.cpp
    src/RecordingControlBar.cpp
    src/RecordingBoundaryOverlay.cpp
    src/RecordingAnnotationOverlay.cpp
    src/RecordingInitTask.cpp
    src/VideoEncoderFactory.cpp
    # Recording visual effects
    include/recording/CursorHighlightEffect.h
    include/recording/SpotlightEffect.h
    src/recording/CursorHighlightEffect.cpp
    src/recording/SpotlightEffect.cpp
    src/encoding/EncoderFactory.cpp
    src/encoding/NativeGifEncoder.cpp
    src/encoding/WebPAnimEncoder.cpp
    # Video player
    src/video/IVideoPlayer.cpp
    src/video/VideoPlaybackWidget.cpp
    src/video/VideoTimeline.cpp
    src/video/TrimTimeline.cpp
    src/video/VideoTrimmer.cpp
    src/video/RecordingPreviewWindow.cpp
    src/video/FormatSelectionWidget.cpp
    src/video/InPlacePreviewOverlay.cpp
    src/video/CountdownOverlay.cpp
    # Video annotation system
    include/video/VideoAnnotation.h
    include/video/AnnotationTrack.h
    include/video/VideoAnnotationRenderer.h
    include/video/AnnotationTimelineWidget.h
    include/video/VideoAnnotationEditor.h
    src/video/VideoAnnotation.cpp
    src/video/AnnotationTrack.cpp
    src/video/VideoAnnotationRenderer.cpp
    src/video/AnnotationTimelineWidget.cpp
    src/video/VideoAnnotationEditor.cpp
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
    # Utility classes
    src/utils/CoordinateHelper.cpp
    # Region selection
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
    # PinWindow components
    src/pinwindow/ImageTransformer.cpp
    src/pinwindow/ResizeHandler.cpp
    src/pinwindow/UIIndicators.cpp
    # Scrolling Capture
    src/scrolling/ScrollingCaptureManager.cpp
    src/scrolling/ScrollingCaptureOverlay.cpp
    src/scrolling/ScrollingCaptureToolbar.cpp
    src/scrolling/ScrollingCaptureThumbnail.cpp
    src/scrolling/ImageStitcher.cpp
    src/scrolling/ImageStitcherAlgorithms.cpp
    src/scrolling/FixedElementDetector.cpp
    src/scrolling/AutoScrollController.cpp
    # Stitching algorithms (new modular stitching system)
    src/stitching/FeatureMatcher.cpp
    src/stitching/OpticalFlowPredictor.cpp
    src/stitching/StaticSceneAnalyzer.cpp
    src/pinwindow/ClickThroughExitButton.cpp
    # Auto-blur detection
    src/detection/FaceDetector.cpp
    src/detection/TextDetector.cpp
    src/detection/AutoBlurManager.cpp
)

set(RESOURCES
    resources/resources.qrc
)

set(HEADERS
    include/MainApplication.h
    include/SettingsDialog.h
    include/CaptureManager.h
    include/RegionSelector.h
    include/PinWindow.h
    include/PinWindowManager.h
    include/annotations/AllAnnotations.h
    include/annotations/AnnotationLayer.h
    include/annotations/AnnotationItem.h
    include/annotations/TextAnnotation.h
    include/annotations/PencilStroke.h
    include/annotations/MarkerStroke.h
    include/annotations/ArrowAnnotation.h
    include/annotations/PolylineAnnotation.h
    include/annotations/ShapeAnnotation.h
    include/annotations/MosaicStroke.h
    include/annotations/MosaicRectAnnotation.h
    include/annotations/StepBadgeAnnotation.h
    include/annotations/EmojiStickerAnnotation.h
    include/annotations/EraserStroke.h
    include/annotations/ErasedItemsGroup.h
    include/annotations/LineStyle.h
    include/ScreenCanvas.h
    include/ScreenCanvasManager.h
    include/AutoLaunchManager.h
    include/IconRenderer.h
    include/ColorPaletteWidget.h
    include/EmojiPicker.h
    include/LineWidthWidget.h
    include/ColorAndWidthWidget.h
    include/ToolbarWidget.h
    include/ToolbarStyle.h
    include/GlassRenderer.h
    include/toolbar/ToolbarButtonConfig.h
    include/toolbar/ToolbarRenderer.h
    include/toolbar/ToolbarTypes.h
    include/AnnotationController.h
    include/ColorPickerDialog.h
    include/InlineTextEditor.h
    include/TextFormattingState.h
    include/PlatformFeatures.h
    include/LaserPointerRenderer.h
    include/ClickRippleRenderer.h
    include/WatermarkRenderer.h
    include/LoadingSpinnerRenderer.h
    include/RecordingManager.h
    include/RecordingRegionSelector.h
    include/RecordingControlBar.h
    include/RecordingBoundaryOverlay.h
    include/RecordingAnnotationOverlay.h
    include/RecordingInitTask.h
    include/IVideoEncoder.h
    include/AVFoundationEncoder.h
    include/MediaFoundationEncoder.h
    include/encoding/EncoderFactory.h
    include/encoding/NativeGifEncoder.h
    include/encoding/WebPAnimEncoder.h
    # Video playback
    include/video/IVideoPlayer.h
    include/video/VideoPlaybackWidget.h
    include/video/VideoTimeline.h
    include/video/TrimTimeline.h
    include/video/VideoTrimmer.h
    include/video/RecordingPreviewWindow.h
    include/video/FormatSelectionWidget.h
    include/video/InPlacePreviewOverlay.h
    include/video/CountdownOverlay.h
    include/TransformationGizmo.h
    include/capture/ICaptureEngine.h
    include/capture/QtCaptureEngine.h
    # Tool system
    include/tools/ToolId.h
    include/tools/ToolDefinition.h
    include/tools/ToolContext.h
    include/tools/IToolHandler.h
    include/tools/ToolRegistry.h
    include/tools/ToolManager.h
    include/tools/handlers/AllHandlers.h
    include/tools/handlers/PencilToolHandler.h
    include/tools/handlers/MarkerToolHandler.h
    include/tools/handlers/ArrowToolHandler.h
    include/tools/handlers/PolylineToolHandler.h
    include/tools/handlers/ShapeToolHandler.h
    include/tools/handlers/MosaicToolHandler.h
    include/tools/handlers/StepBadgeToolHandler.h
    include/tools/handlers/EmojiStickerToolHandler.h
    include/tools/handlers/EraserToolHandler.h
    include/tools/handlers/SelectionToolHandler.h
    include/tools/handlers/LaserPointerToolHandler.h
    # Utility classes
    include/utils/ResourceCleanupHelper.h
    include/utils/CoordinateHelper.h
    # Region selection
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
    include/region/SelectionResizeHelper.h
    # Settings managers
    include/settings/Settings.h
    include/settings/AnnotationSettingsManager.h
    include/settings/FileSettingsManager.h
    include/settings/PinWindowSettingsManager.h
    # UI sections
    include/ui/IWidgetSection.h
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
    # PinWindow components
    include/pinwindow/ImageTransformer.h
    include/pinwindow/ResizeHandler.h
    include/pinwindow/UIIndicators.h
    # Scrolling Capture
    include/scrolling/ScrollingCaptureManager.h
    include/scrolling/ScrollingCaptureOverlay.h
    include/scrolling/ScrollingCaptureToolbar.h
    include/scrolling/ScrollingCaptureThumbnail.h
    include/scrolling/ImageStitcher.h
    include/scrolling/FixedElementDetector.h
    include/scrolling/AutoScrollController.h
    # Stitching algorithms (new modular stitching system)
    include/stitching/FeatureMatcher.h
    include/stitching/OpticalFlowPredictor.h
    include/stitching/StaticSceneAnalyzer.h
    include/pinwindow/ClickThroughExitButton.h
    # Auto-blur detection
    include/detection/IDetector.h
    include/detection/FaceDetector.h
    include/detection/TextDetector.h
    include/detection/AutoBlurManager.h
)

# Cross-platform source files (used by both platforms)
list(APPEND SOURCES
    src/CaptureManager.cpp
    src/ScreenCanvasManager.cpp
    src/capture/ICaptureEngine.cpp
    src/capture/QtCaptureEngine.cpp
    src/capture/IAudioCaptureEngine.cpp
    src/AudioFileWriter.cpp
)

list(APPEND HEADERS
    include/capture/IAudioCaptureEngine.h
    include/AudioFileWriter.h
)

# Platform-specific source files
if(APPLE)
    list(APPEND SOURCES
        src/platform/WindowLevel_mac.mm
        src/platform/PlatformFeatures_mac.mm
        src/WindowDetector.mm
        src/OCRManager.mm
        src/AutoLaunchManager_mac.mm
        src/WindowDetectionOverlay.cpp
        src/OCRController.cpp
        src/capture/SCKCaptureEngine_mac.mm
        src/capture/CoreAudioCaptureEngine_mac.mm
        src/AVFoundationEncoder.mm
        src/video/AVFoundationPlayer_mac.mm
        src/input/MouseClickTracker_mac.mm
        src/scrolling/AutoScrollController_mac.mm
    )
    list(APPEND HEADERS
        include/WindowDetector.h
        include/OCRManager.h
        include/WindowDetectionOverlay.h
        include/OCRController.h
        include/capture/SCKCaptureEngine.h
        include/capture/CoreAudioCaptureEngine.h
        include/input/MouseClickTracker.h
    )
    # Enable ARC for video player (uses __weak references)
    set_source_files_properties(
        src/video/AVFoundationPlayer_mac.mm
        PROPERTIES COMPILE_FLAGS "-fobjc-arc"
    )
elseif(WIN32)
    list(APPEND SOURCES
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
    )
    list(APPEND HEADERS
        include/WindowDetector.h
        include/OCRManager.h
        include/capture/DXGICaptureEngine.h
        include/capture/WASAPIAudioCaptureEngine.h
        include/input/MouseClickTracker.h
    )
    # Windows application icon resource (generated from template)
    set(WIN32_RESOURCES "${CMAKE_BINARY_DIR}/snaptray.rc")
endif()

# Create executable without WIN32 flag (we'll set it conditionally below)
add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS} ${RESOURCES} ${WIN32_RESOURCES})

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
    ${CMAKE_BINARY_DIR}/_deps/opencv-src/modules/core/include
    ${CMAKE_BINARY_DIR}/_deps/opencv-src/modules/imgproc/include
    ${CMAKE_BINARY_DIR}/_deps/opencv-src/modules/features2d/include
    ${CMAKE_BINARY_DIR}/_deps/opencv-src/modules/flann/include
    ${CMAKE_BINARY_DIR}/_deps/opencv-src/modules/objdetect/include
    ${CMAKE_BINARY_DIR}/_deps/opencv-src/modules/calib3d/include
    ${CMAKE_BINARY_DIR}/_deps/opencv-src/modules/video/include
    ${CMAKE_BINARY_DIR}/_deps/libwebp-src/src
)

# Disable qDebug() output in Release builds
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<NOT:$<CONFIG:Debug>>:QT_NO_DEBUG_OUTPUT>
)

# Enable development features only in Debug builds
target_compile_definitions(${PROJECT_NAME} PRIVATE
    $<$<CONFIG:Debug>:SNAPTRAY_ENABLE_DEV_FEATURES>
)

# macOS frameworks needed for window detection, auto-launch, and native encoding
if(APPLE)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    find_library(APPLICATIONSERVICES_FRAMEWORK ApplicationServices)
    find_library(APPKIT_FRAMEWORK AppKit)
    find_library(VISION_FRAMEWORK Vision)
    find_library(SERVICEMANAGEMENT_FRAMEWORK ServiceManagement)
    find_library(COREMEDIA_FRAMEWORK CoreMedia)
    find_library(COREVIDEO_FRAMEWORK CoreVideo)
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation)
    # ScreenCaptureKit is optional (macOS 12.3+), weak link to allow running on older systems
    find_library(SCREENCAPTUREKIT_FRAMEWORK ScreenCaptureKit)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Svg
        Qt6::Concurrent
        QHotkey::QHotkey
        ${COREGRAPHICS_FRAMEWORK}
        ${APPLICATIONSERVICES_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
        ${VISION_FRAMEWORK}
        ${SERVICEMANAGEMENT_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${AVFOUNDATION_FRAMEWORK}
        opencv_core
        opencv_imgproc
        opencv_features2d
        opencv_flann
        opencv_calib3d
        opencv_objdetect
        opencv_video
        webp
        libwebpmux
    )
    # Weak link ScreenCaptureKit if available (for macOS 12.3+ compatibility)
    if(SCREENCAPTUREKIT_FRAMEWORK)
        target_link_libraries(${PROJECT_NAME} PRIVATE
            "-weak_framework ScreenCaptureKit"
        )
    endif()
elseif(WIN32)
    # Windows-specific libraries for WindowDetector, OCR, DXGI capture, Media Foundation, and WASAPI
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Svg
        Qt6::Concurrent
        QHotkey::QHotkey
        dwmapi
        psapi
        windowsapp
        d3d11
        dxgi
        mfreadwrite
        mfplat
        mfuuid
        ole32       # COM for WASAPI audio capture
        propsys     # Property system for audio device enumeration
        opencv_core
        opencv_imgproc
        opencv_features2d
        opencv_flann
        opencv_calib3d
        opencv_objdetect
        opencv_video
        webp
        libwebpmux
    )
    # Enable C++/WinRT for Windows.Media.Ocr
    target_compile_options(${PROJECT_NAME} PRIVATE /await)
else()
    message(FATAL_ERROR "SnapTray only supports macOS and Windows. Current platform is not supported.")
endif()

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
