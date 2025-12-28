cmake_minimum_required(VERSION 3.16...3.31)
project(SnapTray VERSION 1.0.5 LANGUAGES CXX)

# ============================================================================
# Version Configuration
# ============================================================================
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
    set(MACOSX_BUNDLE_BUNDLE_NAME "SnapTray")
    set(MACOSX_BUNDLE_GUI_IDENTIFIER "com.victorfu.snaptray")
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

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Svg Test Concurrent)

# Fetch QHotkey for global hotkey support
include(FetchContent)
FetchContent_Declare(
    QHotkey
    GIT_REPOSITORY https://github.com/Skycoder42/QHotkey.git
    GIT_TAG        master
)
set(QHOTKEY_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(QHotkey)

# ============================================================================
# OpenCV for Scrolling Capture (static build)
# ============================================================================
FetchContent_Declare(
    OpenCV
    GIT_REPOSITORY https://github.com/opencv/opencv.git
    GIT_TAG        4.9.0
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
# Use system zlib to avoid build conflicts on macOS
set(BUILD_ZLIB OFF CACHE BOOL "" FORCE)
# Keep: core, imgproc, features2d, flann
set(BUILD_opencv_core ON CACHE BOOL "" FORCE)
set(BUILD_opencv_imgproc ON CACHE BOOL "" FORCE)
set(BUILD_opencv_features2d ON CACHE BOOL "" FORCE)

FetchContent_MakeAvailable(OpenCV)

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
    src/annotations/ErasedItemsGroup.cpp
    src/annotations/EraserStroke.cpp
    src/annotations/TextAnnotation.cpp
    src/ScreenCanvas.cpp
    src/IconRenderer.cpp
    src/ColorPaletteWidget.cpp
    src/LineWidthWidget.cpp
    src/ColorAndWidthWidget.cpp
    src/ToolbarWidget.cpp
    src/ToolbarStyle.cpp
    src/GlassRenderer.cpp
    src/settings/AnnotationSettingsManager.cpp
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
    src/RecordingInitTask.cpp
    src/VideoEncoderFactory.cpp
    src/encoding/EncoderFactory.cpp
    src/encoding/NativeGifEncoder.cpp
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
    src/region/RadiusSliderWidget.cpp
    # UI sections
    src/ui/sections/ColorSection.cpp
    src/ui/sections/WidthSection.cpp
    src/ui/sections/MosaicWidthSection.cpp
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
    include/annotations/EraserStroke.h
    include/annotations/ErasedItemsGroup.h
    include/ScreenCanvas.h
    include/ScreenCanvasManager.h
    include/AutoLaunchManager.h
    include/IconRenderer.h
    include/ColorPaletteWidget.h
    include/LineWidthWidget.h
    include/ColorAndWidthWidget.h
    include/ToolbarWidget.h
    include/ToolbarStyle.h
    include/GlassRenderer.h
    include/AnnotationController.h
    include/ColorPickerDialog.h
    include/InlineTextEditor.h
    include/PlatformFeatures.h
    include/LaserPointerRenderer.h
    include/ClickRippleRenderer.h
    include/WatermarkRenderer.h
    include/LoadingSpinnerRenderer.h
    include/RecordingManager.h
    include/RecordingRegionSelector.h
    include/RecordingControlBar.h
    include/RecordingBoundaryOverlay.h
    include/RecordingInitTask.h
    include/IVideoEncoder.h
    include/AVFoundationEncoder.h
    include/MediaFoundationEncoder.h
    include/encoding/EncoderFactory.h
    include/encoding/NativeGifEncoder.h
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
    include/region/RadiusSliderWidget.h
    # UI sections
    include/ui/IWidgetSection.h
    include/ui/sections/ColorSection.h
    include/ui/sections/WidthSection.h
    include/ui/sections/MosaicWidthSection.h
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
    )
    list(APPEND HEADERS
        include/WindowDetector.h
        include/OCRManager.h
        include/WindowDetectionOverlay.h
        include/OCRController.h
        include/capture/SCKCaptureEngine.h
        include/capture/CoreAudioCaptureEngine.h
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
    )
    list(APPEND HEADERS
        include/WindowDetector.h
        include/OCRManager.h
        include/capture/DXGICaptureEngine.h
        include/capture/WASAPIAudioCaptureEngine.h
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
    )
    # Enable C++/WinRT for Windows.Media.Ocr
    target_compile_options(${PROJECT_NAME} PRIVATE /await)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets Qt6::Gui Qt6::Svg Qt6::Concurrent QHotkey::QHotkey opencv_core opencv_imgproc opencv_features2d opencv_flann opencv_calib3d opencv_objdetect)
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
        MACOSX_BUNDLE_BUNDLE_NAME "SnapTray"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.victorfu.snaptray"
        MACOSX_BUNDLE_ICON_FILE snaptray.icns
    )
endif()

# ============================================================================
# Testing Configuration
# ============================================================================
enable_testing()
add_subdirectory(tests)
