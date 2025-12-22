cmake_minimum_required(VERSION 3.16...3.31)
project(SnapTray VERSION 1.0.3 LANGUAGES CXX)

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

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui Svg)

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
    src/ScreenCanvas.cpp
    src/IconRenderer.cpp
    src/ColorPaletteWidget.cpp
    src/LineWidthWidget.cpp
    src/ColorAndWidthWidget.cpp
    src/ToolbarWidget.cpp
    src/AnnotationController.cpp
    src/ColorPickerDialog.mm
    src/InlineTextEditor.cpp
    src/MagnifierOverlay.cpp
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
    include/AnnotationLayer.h
    include/ScreenCanvas.h
    include/ScreenCanvasManager.h
    include/AutoLaunchManager.h
    include/IconRenderer.h
    include/ColorPaletteWidget.h
    include/LineWidthWidget.h
    include/ColorAndWidthWidget.h
    include/ToolbarWidget.h
    include/AnnotationController.h
    include/ColorPickerDialog.h
    include/InlineTextEditor.h
    include/MagnifierOverlay.h
    include/PlatformFeatures.h
)

# Cross-platform source files (used by both platforms)
list(APPEND SOURCES
    src/CaptureManager.cpp
    src/ScreenCanvasManager.cpp
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
    )
    list(APPEND HEADERS
        include/WindowDetector.h
        include/OCRManager.h
        include/WindowDetectionOverlay.h
        include/OCRController.h
    )
elseif(WIN32)
    list(APPEND SOURCES
        src/platform/WindowLevel_win.cpp
        src/platform/PlatformFeatures_win.cpp
        src/WindowDetector_win.cpp
        src/OCRManager_win.cpp
        src/AutoLaunchManager_win.cpp
    )
    list(APPEND HEADERS
        include/WindowDetector.h
        include/OCRManager.h
    )
    # Windows application icon resource (generated from template)
    set(WIN32_RESOURCES "${CMAKE_BINARY_DIR}/snaptray.rc")
endif()

# WIN32 flag sets /SUBSYSTEM:WINDOWS on Windows (prevents console window)
# Automatically ignored on non-Windows platforms
add_executable(${PROJECT_NAME} WIN32 ${SOURCES} ${HEADERS} ${RESOURCES} ${WIN32_RESOURCES})

target_include_directories(${PROJECT_NAME} PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

# macOS frameworks needed for window detection and auto-launch
if(APPLE)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    find_library(APPLICATIONSERVICES_FRAMEWORK ApplicationServices)
    find_library(APPKIT_FRAMEWORK AppKit)
    find_library(VISION_FRAMEWORK Vision)
    find_library(SERVICEMANAGEMENT_FRAMEWORK ServiceManagement)
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
    )
elseif(WIN32)
    # Windows-specific libraries for WindowDetector and OCR
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Svg
        QHotkey::QHotkey
        dwmapi
        psapi
        windowsapp
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
