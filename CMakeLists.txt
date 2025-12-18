cmake_minimum_required(VERSION 3.16)
project(SnapTray VERSION 1.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTORCC ON)
set(CMAKE_AUTOUIC ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Gui)

# Fetch QHotkey for global hotkey support
# Set policy version for QHotkey's older CMakeLists.txt
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "" FORCE)
include(FetchContent)
FetchContent_Declare(
    QHotkey
    GIT_REPOSITORY https://github.com/Skycoder42/QHotkey.git
    GIT_TAG        1.5.0
)
set(QHOTKEY_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(QHotkey)

set(SOURCES
    src/main.cpp
    src/MainApplication.cpp
    src/SettingsDialog.cpp
    src/CaptureManager.cpp
    src/RegionSelector.cpp
    src/PinWindow.cpp
    src/PinWindowManager.cpp
    src/AnnotationLayer.cpp
)

set(HEADERS
    include/MainApplication.h
    include/SettingsDialog.h
    include/CaptureManager.h
    include/RegionSelector.h
    include/PinWindow.h
    include/PinWindowManager.h
    include/AnnotationLayer.h
)

# macOS-specific source files
if(APPLE)
    list(APPEND SOURCES
        src/WindowDetector.mm
        src/OCRManager.mm
    )
    list(APPEND HEADERS
        include/WindowDetector.h
        include/OCRManager.h
    )
endif()

add_executable(${PROJECT_NAME} ${SOURCES} ${HEADERS})

target_include_directories(${PROJECT_NAME} PRIVATE ${CMAKE_SOURCE_DIR}/include)

# macOS frameworks needed for window detection
if(APPLE)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    find_library(APPLICATIONSERVICES_FRAMEWORK ApplicationServices)
    find_library(APPKIT_FRAMEWORK AppKit)
    find_library(VISION_FRAMEWORK Vision)
    target_link_libraries(${PROJECT_NAME} PRIVATE
        Qt6::Widgets
        Qt6::Gui
        QHotkey::QHotkey
        ${COREGRAPHICS_FRAMEWORK}
        ${APPLICATIONSERVICES_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
        ${VISION_FRAMEWORK}
    )
else()
    target_link_libraries(${PROJECT_NAME} PRIVATE Qt6::Widgets Qt6::Gui QHotkey::QHotkey)
endif()

# macOS-specific settings
if(APPLE)
    set_target_properties(${PROJECT_NAME} PROPERTIES
        MACOSX_BUNDLE TRUE
        MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/Info.plist
        MACOSX_BUNDLE_BUNDLE_NAME "SnapTray"
        MACOSX_BUNDLE_BUNDLE_VERSION "1.0.0"
        MACOSX_BUNDLE_SHORT_VERSION_STRING "1.0.0"
        MACOSX_BUNDLE_GUI_IDENTIFIER "com.mysoft.snaptray"
    )
endif()
