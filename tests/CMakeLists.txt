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
    ${CMAKE_SOURCE_DIR}/src/pinwindow/ClickThroughExitButton.cpp
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
    ${CMAKE_SOURCE_DIR}/include/pinwindow/ClickThroughExitButton.h
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
        QHotkey::QHotkey
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

add_pinwindow_test(PinWindow_ClickThrough
    PinWindow/tst_ClickThrough.cpp)

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

# SelectionStateManager test
set(SELECTIONSTATEMANAGER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/region/SelectionStateManager.cpp
)

set(SELECTIONSTATEMANAGER_HEADERS
    ${CMAKE_SOURCE_DIR}/include/region/SelectionStateManager.h
)

add_executable(RegionSelector_SelectionStateManager
    RegionSelector/tst_SelectionStateManager.cpp
    ${SELECTIONSTATEMANAGER_SOURCES}
    ${SELECTIONSTATEMANAGER_HEADERS}
)

target_include_directories(RegionSelector_SelectionStateManager PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(RegionSelector_SelectionStateManager PRIVATE
    Qt6::Widgets
    Qt6::Gui
    Qt6::Test
)

add_test(NAME RegionSelector_SelectionStateManager COMMAND RegionSelector_SelectionStateManager)

set_tests_properties(RegionSelector_SelectionStateManager PROPERTIES
    TIMEOUT 60
)

# ============================================================================
# AudioFileWriter Tests
# ============================================================================

set(AUDIOFILEWRITER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/AudioFileWriter.cpp
)

set(AUDIOFILEWRITER_HEADERS
    ${CMAKE_SOURCE_DIR}/include/AudioFileWriter.h
)

add_executable(Audio_AudioFileWriter
    Audio/tst_AudioFileWriter.cpp
    ${AUDIOFILEWRITER_SOURCES}
    ${AUDIOFILEWRITER_HEADERS}
)

target_include_directories(Audio_AudioFileWriter PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(Audio_AudioFileWriter PRIVATE
    Qt6::Core
    Qt6::Test
    Qt6::Concurrent
)

add_test(NAME Audio_AudioFileWriter COMMAND Audio_AudioFileWriter)

set_tests_properties(Audio_AudioFileWriter PROPERTIES
    TIMEOUT 60
)

# ============================================================================
# NativeGifEncoder Tests
# ============================================================================

set(NATIVEGIFENCODER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/encoding/NativeGifEncoder.cpp
)

set(NATIVEGIFENCODER_HEADERS
    ${CMAKE_SOURCE_DIR}/include/encoding/NativeGifEncoder.h
)

add_executable(Encoding_NativeGifEncoder
    Encoding/tst_NativeGifEncoder.cpp
    ${NATIVEGIFENCODER_SOURCES}
    ${NATIVEGIFENCODER_HEADERS}
)

target_include_directories(Encoding_NativeGifEncoder PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(Encoding_NativeGifEncoder PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

add_test(NAME Encoding_NativeGifEncoder COMMAND Encoding_NativeGifEncoder)

set_tests_properties(Encoding_NativeGifEncoder PROPERTIES
    TIMEOUT 120
)

# ============================================================================
# EncoderFactory Tests
# ============================================================================

set(ENCODERFACTORY_SOURCES
    ${CMAKE_SOURCE_DIR}/src/encoding/EncoderFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/encoding/NativeGifEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/VideoEncoderFactory.cpp
)

set(ENCODERFACTORY_HEADERS
    ${CMAKE_SOURCE_DIR}/include/encoding/EncoderFactory.h
    ${CMAKE_SOURCE_DIR}/include/encoding/NativeGifEncoder.h
    ${CMAKE_SOURCE_DIR}/include/IVideoEncoder.h
)

# Platform-specific encoder sources
if(APPLE)
    list(APPEND ENCODERFACTORY_SOURCES
        ${CMAKE_SOURCE_DIR}/src/AVFoundationEncoder.mm
    )
    list(APPEND ENCODERFACTORY_HEADERS
        ${CMAKE_SOURCE_DIR}/include/AVFoundationEncoder.h
    )
elseif(WIN32)
    list(APPEND ENCODERFACTORY_SOURCES
        ${CMAKE_SOURCE_DIR}/src/MediaFoundationEncoder.cpp
    )
    list(APPEND ENCODERFACTORY_HEADERS
        ${CMAKE_SOURCE_DIR}/include/MediaFoundationEncoder.h
    )
endif()

add_executable(Encoding_EncoderFactory
    Encoding/tst_EncoderFactory.cpp
    ${ENCODERFACTORY_SOURCES}
    ${ENCODERFACTORY_HEADERS}
)

target_include_directories(Encoding_EncoderFactory PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(Encoding_EncoderFactory PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Test
)

# Platform-specific frameworks for encoder tests
if(APPLE)
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation)
    find_library(COREMEDIA_FRAMEWORK CoreMedia)
    find_library(COREVIDEO_FRAMEWORK CoreVideo)
    target_link_libraries(Encoding_EncoderFactory PRIVATE
        ${AVFOUNDATION_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
    )
elseif(WIN32)
    target_link_libraries(Encoding_EncoderFactory PRIVATE
        mfplat
        mfreadwrite
        mfuuid
    )
endif()

add_test(NAME Encoding_EncoderFactory COMMAND Encoding_EncoderFactory)

set_tests_properties(Encoding_EncoderFactory PROPERTIES
    TIMEOUT 120
)

# ============================================================================
# RecordingManager Tests
# ============================================================================

# NOTE: RecordingManager tests require linking with the main application's
# compiled objects due to complex Qt MOC dependencies. These tests are
# configured but may require additional setup to build correctly.
#
# TODO: Consider one of these approaches:
# 1. Create a static library target for recording components
# 2. Use dependency injection to allow mock testing
# 3. Link against the main application's object files
#
# For now, we provide simplified tests that work with available infrastructure.

# RecordingManager has many dependencies; we test with minimal coupling
set(RECORDINGMANAGER_HEADERS
    ${CMAKE_SOURCE_DIR}/include/RecordingManager.h
    ${CMAKE_SOURCE_DIR}/include/RecordingInitTask.h
    ${CMAKE_SOURCE_DIR}/include/RecordingRegionSelector.h
    ${CMAKE_SOURCE_DIR}/include/RecordingControlBar.h
    ${CMAKE_SOURCE_DIR}/include/RecordingBoundaryOverlay.h
    ${CMAKE_SOURCE_DIR}/include/encoding/NativeGifEncoder.h
    ${CMAKE_SOURCE_DIR}/include/AudioFileWriter.h
    ${CMAKE_SOURCE_DIR}/include/capture/QtCaptureEngine.h
    ${CMAKE_SOURCE_DIR}/include/IVideoEncoder.h
    ${CMAKE_SOURCE_DIR}/include/capture/ICaptureEngine.h
    ${CMAKE_SOURCE_DIR}/include/capture/IAudioCaptureEngine.h
)

if(APPLE)
    list(APPEND RECORDINGMANAGER_HEADERS
        ${CMAKE_SOURCE_DIR}/include/capture/SCKCaptureEngine.h
        ${CMAKE_SOURCE_DIR}/include/capture/CoreAudioCaptureEngine.h
        ${CMAKE_SOURCE_DIR}/include/AVFoundationEncoder.h
    )
endif()

# The following tests are temporarily disabled due to complex factory dependencies.
# They require linking with CaptureEngineFactory, VideoEncoderFactory, and
# AudioCaptureEngineFactory implementations which are defined in platform-specific
# source files that need to be included.
#
# To enable these tests, either:
# 1. Add the factory source files (e.g., src/CaptureEngineFactory.cpp)
# 2. Create a shared library target for recording components
# 3. Use the mock classes in tests/mocks/ with dependency injection

if(FALSE)  # Temporarily disabled

# StateMachine test - tests state machine logic
add_executable(RecordingManager_StateMachine
    RecordingManager/tst_StateMachine.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingManager.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingInitTask.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingRegionSelector.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingControlBar.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingBoundaryOverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/encoding/EncoderFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/encoding/NativeGifEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/AudioFileWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/capture/QtCaptureEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/CoordinateHelper.cpp
    ${CMAKE_SOURCE_DIR}/src/IconRenderer.cpp
    ${RECORDINGMANAGER_HEADERS}
)

# Add platform-specific sources
if(APPLE)
    target_sources(RecordingManager_StateMachine PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/WindowLevel_mac.mm
        ${CMAKE_SOURCE_DIR}/src/capture/SCKCaptureEngine_mac.mm
        ${CMAKE_SOURCE_DIR}/src/capture/CoreAudioCaptureEngine_mac.mm
        ${CMAKE_SOURCE_DIR}/src/AVFoundationEncoder.mm
    )
elseif(WIN32)
    target_sources(RecordingManager_StateMachine PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/WindowLevel_win.cpp
        ${CMAKE_SOURCE_DIR}/src/capture/DXGICaptureEngine_win.cpp
        ${CMAKE_SOURCE_DIR}/src/capture/WASAPIAudioCaptureEngine_win.cpp
        ${CMAKE_SOURCE_DIR}/src/MediaFoundationEncoder.cpp
    )
endif()

target_include_directories(RecordingManager_StateMachine PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(RecordingManager_StateMachine PRIVATE
    Qt6::Widgets
    Qt6::Gui
    Qt6::Svg
    Qt6::Test
    Qt6::Concurrent
)

if(APPLE)
    find_library(AVFOUNDATION_FRAMEWORK AVFoundation)
    find_library(COREMEDIA_FRAMEWORK CoreMedia)
    find_library(COREVIDEO_FRAMEWORK CoreVideo)
    find_library(COREAUDIO_FRAMEWORK CoreAudio)
    find_library(AUDIOTOOLBOX_FRAMEWORK AudioToolbox)
    find_library(SCREENCAPTUREKIT_FRAMEWORK ScreenCaptureKit)
    find_library(COREGRAPHICS_FRAMEWORK CoreGraphics)
    find_library(APPKIT_FRAMEWORK AppKit)
    target_link_libraries(RecordingManager_StateMachine PRIVATE
        ${AVFOUNDATION_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
    )
    if(SCREENCAPTUREKIT_FRAMEWORK)
        target_link_libraries(RecordingManager_StateMachine PRIVATE
            ${SCREENCAPTUREKIT_FRAMEWORK}
        )
    endif()
elseif(WIN32)
    target_link_libraries(RecordingManager_StateMachine PRIVATE
        mfplat mfreadwrite mfuuid d3d11 dxgi
    )
endif()

add_test(NAME RecordingManager_StateMachine COMMAND RecordingManager_StateMachine)
set_tests_properties(RecordingManager_StateMachine PROPERTIES TIMEOUT 60)

# Lifecycle test - reuses same sources
add_executable(RecordingManager_Lifecycle
    RecordingManager/tst_Lifecycle.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingManager.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingInitTask.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingRegionSelector.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingControlBar.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingBoundaryOverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/encoding/EncoderFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/encoding/NativeGifEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/AudioFileWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/capture/QtCaptureEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/CoordinateHelper.cpp
    ${CMAKE_SOURCE_DIR}/src/IconRenderer.cpp
)

if(APPLE)
    target_sources(RecordingManager_Lifecycle PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/WindowLevel_mac.mm
        ${CMAKE_SOURCE_DIR}/src/capture/SCKCaptureEngine_mac.mm
        ${CMAKE_SOURCE_DIR}/src/capture/CoreAudioCaptureEngine_mac.mm
        ${CMAKE_SOURCE_DIR}/src/AVFoundationEncoder.mm
    )
elseif(WIN32)
    target_sources(RecordingManager_Lifecycle PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/WindowLevel_win.cpp
        ${CMAKE_SOURCE_DIR}/src/capture/DXGICaptureEngine_win.cpp
        ${CMAKE_SOURCE_DIR}/src/capture/WASAPIAudioCaptureEngine_win.cpp
        ${CMAKE_SOURCE_DIR}/src/MediaFoundationEncoder.cpp
    )
endif()

target_include_directories(RecordingManager_Lifecycle PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(RecordingManager_Lifecycle PRIVATE
    Qt6::Widgets
    Qt6::Gui
    Qt6::Svg
    Qt6::Test
    Qt6::Concurrent
)

if(APPLE)
    target_link_libraries(RecordingManager_Lifecycle PRIVATE
        ${AVFOUNDATION_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
    )
    if(SCREENCAPTUREKIT_FRAMEWORK)
        target_link_libraries(RecordingManager_Lifecycle PRIVATE
            ${SCREENCAPTUREKIT_FRAMEWORK}
        )
    endif()
elseif(WIN32)
    target_link_libraries(RecordingManager_Lifecycle PRIVATE
        mfplat mfreadwrite mfuuid d3d11 dxgi
    )
endif()

add_test(NAME RecordingManager_Lifecycle COMMAND RecordingManager_Lifecycle)
set_tests_properties(RecordingManager_Lifecycle PROPERTIES TIMEOUT 60)

# AudioIntegration test
add_executable(RecordingManager_AudioIntegration
    RecordingManager/tst_AudioIntegration.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingManager.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingInitTask.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingRegionSelector.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingControlBar.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingBoundaryOverlay.cpp
    ${CMAKE_SOURCE_DIR}/src/encoding/EncoderFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/encoding/NativeGifEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/AudioFileWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/capture/QtCaptureEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/utils/CoordinateHelper.cpp
    ${CMAKE_SOURCE_DIR}/src/IconRenderer.cpp
)

if(APPLE)
    target_sources(RecordingManager_AudioIntegration PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/WindowLevel_mac.mm
        ${CMAKE_SOURCE_DIR}/src/capture/SCKCaptureEngine_mac.mm
        ${CMAKE_SOURCE_DIR}/src/capture/CoreAudioCaptureEngine_mac.mm
        ${CMAKE_SOURCE_DIR}/src/AVFoundationEncoder.mm
    )
elseif(WIN32)
    target_sources(RecordingManager_AudioIntegration PRIVATE
        ${CMAKE_SOURCE_DIR}/src/platform/WindowLevel_win.cpp
        ${CMAKE_SOURCE_DIR}/src/capture/DXGICaptureEngine_win.cpp
        ${CMAKE_SOURCE_DIR}/src/capture/WASAPIAudioCaptureEngine_win.cpp
        ${CMAKE_SOURCE_DIR}/src/MediaFoundationEncoder.cpp
    )
endif()

target_include_directories(RecordingManager_AudioIntegration PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(RecordingManager_AudioIntegration PRIVATE
    Qt6::Widgets
    Qt6::Gui
    Qt6::Svg
    Qt6::Test
    Qt6::Concurrent
)

if(APPLE)
    target_link_libraries(RecordingManager_AudioIntegration PRIVATE
        ${AVFOUNDATION_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
        ${APPKIT_FRAMEWORK}
    )
    if(SCREENCAPTUREKIT_FRAMEWORK)
        target_link_libraries(RecordingManager_AudioIntegration PRIVATE
            ${SCREENCAPTUREKIT_FRAMEWORK}
        )
    endif()
elseif(WIN32)
    target_link_libraries(RecordingManager_AudioIntegration PRIVATE
        mfplat mfreadwrite mfuuid d3d11 dxgi
    )
endif()

add_test(NAME RecordingManager_AudioIntegration COMMAND RecordingManager_AudioIntegration)
set_tests_properties(RecordingManager_AudioIntegration PROPERTIES TIMEOUT 60)

# ============================================================================
# RecordingInitTask Tests
# ============================================================================

add_executable(RecordingManager_InitTask
    RecordingManager/tst_InitTask.cpp
    ${CMAKE_SOURCE_DIR}/src/RecordingInitTask.cpp
    ${CMAKE_SOURCE_DIR}/src/encoding/EncoderFactory.cpp
    ${CMAKE_SOURCE_DIR}/src/encoding/NativeGifEncoder.cpp
    ${CMAKE_SOURCE_DIR}/src/AudioFileWriter.cpp
    ${CMAKE_SOURCE_DIR}/src/capture/QtCaptureEngine.cpp
)

if(APPLE)
    target_sources(RecordingManager_InitTask PRIVATE
        ${CMAKE_SOURCE_DIR}/src/capture/SCKCaptureEngine_mac.mm
        ${CMAKE_SOURCE_DIR}/src/capture/CoreAudioCaptureEngine_mac.mm
        ${CMAKE_SOURCE_DIR}/src/AVFoundationEncoder.mm
    )
elseif(WIN32)
    target_sources(RecordingManager_InitTask PRIVATE
        ${CMAKE_SOURCE_DIR}/src/capture/DXGICaptureEngine_win.cpp
        ${CMAKE_SOURCE_DIR}/src/capture/WASAPIAudioCaptureEngine_win.cpp
        ${CMAKE_SOURCE_DIR}/src/MediaFoundationEncoder.cpp
    )
endif()

target_include_directories(RecordingManager_InitTask PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/external
    ${CMAKE_BINARY_DIR}/generated
)

target_link_libraries(RecordingManager_InitTask PRIVATE
    Qt6::Core
    Qt6::Gui
    Qt6::Widgets
    Qt6::Test
    Qt6::Concurrent
)

if(APPLE)
    target_link_libraries(RecordingManager_InitTask PRIVATE
        ${AVFOUNDATION_FRAMEWORK}
        ${COREMEDIA_FRAMEWORK}
        ${COREVIDEO_FRAMEWORK}
        ${COREAUDIO_FRAMEWORK}
        ${AUDIOTOOLBOX_FRAMEWORK}
        ${COREGRAPHICS_FRAMEWORK}
    )
    if(SCREENCAPTUREKIT_FRAMEWORK)
        target_link_libraries(RecordingManager_InitTask PRIVATE
            ${SCREENCAPTUREKIT_FRAMEWORK}
        )
    endif()
elseif(WIN32)
    target_link_libraries(RecordingManager_InitTask PRIVATE
        mfplat mfreadwrite mfuuid d3d11 dxgi
    )
endif()

add_test(NAME RecordingManager_InitTask COMMAND RecordingManager_InitTask)
set_tests_properties(RecordingManager_InitTask PROPERTIES TIMEOUT 60)

endif()  # End of temporarily disabled RecordingManager tests
