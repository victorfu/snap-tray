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

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Svg Test)

# Fetch QHotkey for global hotkey support
include(FetchContent)
FetchContent_Declare(
    QHotkey
    GIT_REPOSITORY https://github.com/Skycoder42/QHotkey.git
    GIT_TAG        master
)
set(QHOTKEY_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(QHotkey)

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
    src/annotations/ShapeAnnotation.cpp
    src/annotations/MosaicStroke.cpp
    src/annotations/StepBadgeAnnotation.cpp
    src/annotations/ErasedItemsGroup.cpp
    src/annotations/TextAnnotation.cpp
    src/ScreenCanvas.cpp
    src/IconRenderer.cpp
    src/ColorPaletteWidget.cpp
    src/LineWidthWidget.cpp
    src/ColorAndWidthWidget.cpp
    src/ToolbarWidget.cpp
    src/ToolbarStyle.cpp
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
    src/FFmpegEncoder.cpp
    src/VideoEncoderFactory.cpp
    src/encoding/EncoderFactory.cpp
    src/TransformationGizmo.cpp
    # Tool system
    src/tools/ToolRegistry.cpp
    src/tools/ToolManager.cpp
    src/tools/handlers/PencilToolHandler.cpp
    src/tools/handlers/MarkerToolHandler.cpp
    src/tools/handlers/ArrowToolHandler.cpp
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
    # UI sections
    src/ui/sections/ColorSection.cpp
    src/ui/sections/WidthSection.cpp
    src/ui/sections/TextSection.cpp
    src/ui/sections/ArrowStyleSection.cpp
    src/ui/sections/ShapeSection.cpp
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
    include/annotations/ShapeAnnotation.h
    include/annotations/MosaicStroke.h
    include/annotations/StepBadgeAnnotation.h
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
    include/FFmpegEncoder.h
    include/IVideoEncoder.h
    include/AVFoundationEncoder.h
    include/MediaFoundationEncoder.h
    include/encoding/EncoderFactory.h
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
    # UI sections
    include/ui/IWidgetSection.h
    include/ui/sections/ColorSection.h
    include/ui/sections/WidthSection.h
    include/ui/sections/TextSection.h
    include/ui/sections/ArrowStyleSection.h
    include/ui/sections/ShapeSection.h
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
        QHotkey::QHotkey
        ${COREGRAPHICS_FRAMEWORK}
        ${APPLICATIONSERVICES_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
        ${VISION_FRAMEWORK}
        ${SERVICEMANAGEMENT_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${AVFOUNDATION_FRAMEWORK}
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
    )
    # Enable C++/WinRT for Windows.Media.Ocr
    target_compile_options(${PROJECT_NAME} PRIVATE /await)
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets Qt6::Gui Qt6::Svg QHotkey::QHotkey)
endif()

# macOS-specific settings
if(APPLE)
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
