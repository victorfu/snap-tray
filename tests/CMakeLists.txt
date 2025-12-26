# ============================================================================
# SnapTray Tests Configuration
# ============================================================================

# Enable automoc for tests
set(CMAKE_AUTOMOC ON)

# Common source files needed for testing ColorAndWidthWidget
set(COLORANDWIDTHWIDGET_COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/src/ColorAndWidthWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/ToolbarStyle.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sections/ColorSection.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sections/WidthSection.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sections/TextSection.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sections/ArrowStyleSection.cpp
    ${CMAKE_SOURCE_DIR}/src/ui/sections/ShapeSection.cpp
)

# Common headers that need MOC processing
set(COLORANDWIDTHWIDGET_COMMON_HEADERS
    ${CMAKE_SOURCE_DIR}/include/ColorAndWidthWidget.h
    ${CMAKE_SOURCE_DIR}/include/ui/sections/ColorSection.h
    ${CMAKE_SOURCE_DIR}/include/ui/sections/WidthSection.h
    ${CMAKE_SOURCE_DIR}/include/ui/sections/TextSection.h
    ${CMAKE_SOURCE_DIR}/include/ui/sections/ArrowStyleSection.h
    ${CMAKE_SOURCE_DIR}/include/ui/sections/ShapeSection.h
)

# Helper function to create a test executable
function(add_colorandwidthwidget_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME}
        ${TEST_SOURCE}
        ${COLORANDWIDTHWIDGET_COMMON_SOURCES}
        ${COLORANDWIDTHWIDGET_COMMON_HEADERS}
    )

    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
    )

    target_link_libraries(${TEST_NAME} PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Svg
        Qt6::Test
    )

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT 60
    )
endfunction()

# Create individual test executables
add_colorandwidthwidget_test(ColorAndWidthWidget_State
    ColorAndWidthWidget/tst_State.cpp)

add_colorandwidthwidget_test(ColorAndWidthWidget_Signals
    ColorAndWidthWidget/tst_Signals.cpp)

add_colorandwidthwidget_test(ColorAndWidthWidget_HitTest
    ColorAndWidthWidget/tst_HitTest.cpp)

add_colorandwidthwidget_test(ColorAndWidthWidget_Events
    ColorAndWidthWidget/tst_Events.cpp)

# ============================================================================
# PinWindow Tests
# ============================================================================

# Common source files needed for testing PinWindow
set(PINWINDOW_COMMON_SOURCES
    ${CMAKE_SOURCE_DIR}/src/PinWindow.cpp
    ${CMAKE_SOURCE_DIR}/src/PinWindowManager.cpp
    ${CMAKE_SOURCE_DIR}/src/WatermarkRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/LoadingSpinnerRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/pinwindow/ResizeHandler.cpp
    ${CMAKE_SOURCE_DIR}/src/pinwindow/UIIndicators.cpp
)

# Platform-specific sources for PinWindow tests
if(APPLE)
    list(APPEND PINWINDOW_COMMON_SOURCES
        ${CMAKE_SOURCE_DIR}/src/platform/PlatformFeatures_mac.mm
        ${CMAKE_SOURCE_DIR}/src/platform/WindowLevel_mac.mm
        ${CMAKE_SOURCE_DIR}/src/OCRManager.mm
        ${CMAKE_SOURCE_DIR}/src/WindowDetector.mm
    )
elseif(WIN32)
    list(APPEND PINWINDOW_COMMON_SOURCES
        ${CMAKE_SOURCE_DIR}/src/platform/PlatformFeatures_win.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/WindowLevel_win.cpp
        ${CMAKE_SOURCE_DIR}/src/OCRManager_win.cpp
        ${CMAKE_SOURCE_DIR}/src/WindowDetector_win.cpp
    )
endif()

# Common headers that need MOC processing
set(PINWINDOW_COMMON_HEADERS
    ${CMAKE_SOURCE_DIR}/include/PinWindow.h
    ${CMAKE_SOURCE_DIR}/include/PinWindowManager.h
    ${CMAKE_SOURCE_DIR}/include/WatermarkRenderer.h
    ${CMAKE_SOURCE_DIR}/include/LoadingSpinnerRenderer.h
    ${CMAKE_SOURCE_DIR}/include/PlatformFeatures.h
    ${CMAKE_SOURCE_DIR}/include/OCRManager.h
    ${CMAKE_SOURCE_DIR}/include/WindowDetector.h
    ${CMAKE_SOURCE_DIR}/include/pinwindow/ResizeHandler.h
    ${CMAKE_SOURCE_DIR}/include/pinwindow/UIIndicators.h
)

# Helper function to create a PinWindow test executable
function(add_pinwindow_test TEST_NAME TEST_SOURCE)
    add_executable(${TEST_NAME}
        ${TEST_SOURCE}
        ${PINWINDOW_COMMON_SOURCES}
        ${PINWINDOW_COMMON_HEADERS}
    )

    target_include_directories(${TEST_NAME} PRIVATE
        ${CMAKE_SOURCE_DIR}/include
        ${CMAKE_SOURCE_DIR}/src
        ${CMAKE_BINARY_DIR}/generated
    )

    target_link_libraries(${TEST_NAME} PRIVATE
        Qt6::Widgets
        Qt6::Gui
        Qt6::Svg
        Qt6::Test
    )

    # Platform-specific frameworks for PinWindow tests
    if(APPLE)
        find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
        find_library(APPKIT_FRAMEWORK AppKit)
        find_library(VISION_FRAMEWORK Vision)
        target_link_libraries(${TEST_NAME} PRIVATE
            ${COREGRAPHICS_FRAMEWORK}
            ${APPKIT_FRAMEWORK}
            ${VISION_FRAMEWORK}
        )
    endif()

    add_test(NAME ${TEST_NAME} COMMAND ${TEST_NAME})

    set_tests_properties(${TEST_NAME} PROPERTIES
        TIMEOUT 60
    )
endfunction()

# Create PinWindow test executables
add_pinwindow_test(PinWindow_Transform
    PinWindow/tst_Transform.cpp)

add_pinwindow_test(PinWindow_Resize
    PinWindow/tst_Resize.cpp)

# ============================================================================
# RegionSelector Tests
# ============================================================================

# Source files needed for testing MagnifierPanel
set(MAGNIFIERPANEL_SOURCES
    ${CMAKE_SOURCE_DIR}/src/region/MagnifierPanel.cpp
)

set(MAGNIFIERPANEL_HEADERS
    ${CMAKE_SOURCE_DIR}/include/region/MagnifierPanel.h
)

# Source files needed for testing UpdateThrottler
set(UPDATETHROTTLER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/region/UpdateThrottler.cpp
)

set(UPDATETHROTTLER_HEADERS
    ${CMAKE_SOURCE_DIR}/include/region/UpdateThrottler.h
)

# MagnifierPanel test
add_executable(RegionSelector_MagnifierPanel
    RegionSelector/tst_MagnifierPanel.cpp
    ${MAGNIFIERPANEL_SOURCES}
    ${MAGNIFIERPANEL_HEADERS}
)

target_include_directories(RegionSelector_MagnifierPanel PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(RegionSelector_MagnifierPanel PRIVATE
    Qt6::Widgets
    Qt6::Gui
    Qt6::Test
)

add_test(NAME RegionSelector_MagnifierPanel COMMAND RegionSelector_MagnifierPanel)

set_tests_properties(RegionSelector_MagnifierPanel PROPERTIES
    TIMEOUT 60
)

# UpdateThrottler test
add_executable(RegionSelector_UpdateThrottler
    RegionSelector/tst_UpdateThrottler.cpp
    ${UPDATETHROTTLER_SOURCES}
    ${UPDATETHROTTLER_HEADERS}
)

target_include_directories(RegionSelector_UpdateThrottler PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(RegionSelector_UpdateThrottler PRIVATE
    Qt6::Widgets
    Qt6::Gui
    Qt6::Test
)

add_test(NAME RegionSelector_UpdateThrottler COMMAND RegionSelector_UpdateThrottler)

set_tests_properties(RegionSelector_UpdateThrottler PROPERTIES
    TIMEOUT 60
)

# TextAnnotationEditor logic test (tests core logic without full dependencies)
add_executable(RegionSelector_TextAnnotationEditor
    RegionSelector/tst_TextAnnotationEditor.cpp
)

target_include_directories(RegionSelector_TextAnnotationEditor PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(RegionSelector_TextAnnotationEditor PRIVATE
    Qt6::Widgets
    Qt6::Gui
    Qt6::Test
)

add_test(NAME RegionSelector_TextAnnotationEditor COMMAND RegionSelector_TextAnnotationEditor)

set_tests_properties(RegionSelector_TextAnnotationEditor PROPERTIES
    TIMEOUT 60
)
