# ============================================================================
# SnapTray Tests Configuration
# ============================================================================
# Tests now link against library targets instead of compiling source files directly.
# This provides faster incremental builds and ensures tests use the same code as the app.
# ============================================================================

# Find Qt6 Test component for unit tests
find_package(Qt6 REQUIRED COMPONENTS Test)

# Enable automoc for tests
set(CMAKE_AUTOMOC ON)

# ============================================================================
# ToolOptionsPanel Tests (link snaptray_ui)
# ============================================================================

add_executable(ToolOptionsPanel_State ToolOptionsPanel/tst_State.cpp)
target_link_libraries(ToolOptionsPanel_State PRIVATE snaptray_ui Qt6::Test)
add_test(NAME ToolOptionsPanel_State COMMAND ToolOptionsPanel_State)
set_tests_properties(ToolOptionsPanel_State PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(ToolOptionsPanel_Signals ToolOptionsPanel/tst_Signals.cpp)
target_link_libraries(ToolOptionsPanel_Signals PRIVATE snaptray_ui Qt6::Test)
add_test(NAME ToolOptionsPanel_Signals COMMAND ToolOptionsPanel_Signals)
set_tests_properties(ToolOptionsPanel_Signals PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(ToolOptionsPanel_HitTest ToolOptionsPanel/tst_HitTest.cpp)
target_link_libraries(ToolOptionsPanel_HitTest PRIVATE snaptray_ui Qt6::Test)
add_test(NAME ToolOptionsPanel_HitTest COMMAND ToolOptionsPanel_HitTest)
set_tests_properties(ToolOptionsPanel_HitTest PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(ToolOptionsPanel_Events ToolOptionsPanel/tst_Events.cpp)
target_link_libraries(ToolOptionsPanel_Events PRIVATE snaptray_ui Qt6::Test)
add_test(NAME ToolOptionsPanel_Events COMMAND ToolOptionsPanel_Events)
set_tests_properties(ToolOptionsPanel_Events PROPERTIES TIMEOUT 60 LABELS "unit")

# ============================================================================
# PinWindow Tests (link snaptray_ui which includes snaptray_platform)
# ============================================================================

add_executable(PinWindow_Transform PinWindow/tst_Transform.cpp)
target_link_libraries(PinWindow_Transform PRIVATE snaptray_ui Qt6::Test)
add_test(NAME PinWindow_Transform COMMAND PinWindow_Transform)
set_tests_properties(PinWindow_Transform PROPERTIES TIMEOUT 60 LABELS "unit;integration")

add_executable(PinWindow_Resize PinWindow/tst_Resize.cpp)
target_link_libraries(PinWindow_Resize PRIVATE snaptray_ui Qt6::Test)
add_test(NAME PinWindow_Resize COMMAND PinWindow_Resize)
set_tests_properties(PinWindow_Resize PROPERTIES TIMEOUT 60 LABELS "unit;integration")

add_executable(PinWindow_ClickThrough PinWindow/tst_ClickThrough.cpp)
target_link_libraries(PinWindow_ClickThrough PRIVATE snaptray_ui Qt6::Test)
add_test(NAME PinWindow_ClickThrough COMMAND PinWindow_ClickThrough)
set_tests_properties(PinWindow_ClickThrough PROPERTIES TIMEOUT 60 LABELS "unit;integration")

add_executable(PinWindow_ImageTransformer PinWindow/tst_ImageTransformer.cpp)
target_link_libraries(PinWindow_ImageTransformer PRIVATE snaptray_ui Qt6::Test)
add_test(NAME PinWindow_ImageTransformer COMMAND PinWindow_ImageTransformer)
set_tests_properties(PinWindow_ImageTransformer PROPERTIES TIMEOUT 60 LABELS "unit")

# ============================================================================
# RegionSelector Tests
# ============================================================================

add_executable(RegionSelector_MagnifierPanel RegionSelector/tst_MagnifierPanel.cpp)
target_link_libraries(RegionSelector_MagnifierPanel PRIVATE snaptray_ui Qt6::Test)
add_test(NAME RegionSelector_MagnifierPanel COMMAND RegionSelector_MagnifierPanel)
set_tests_properties(RegionSelector_MagnifierPanel PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(RegionSelector_UpdateThrottler RegionSelector/tst_UpdateThrottler.cpp)
target_link_libraries(RegionSelector_UpdateThrottler PRIVATE snaptray_ui Qt6::Test)
add_test(NAME RegionSelector_UpdateThrottler COMMAND RegionSelector_UpdateThrottler)
set_tests_properties(RegionSelector_UpdateThrottler PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(RegionSelector_TextAnnotationEditor RegionSelector/tst_TextAnnotationEditor.cpp)
target_link_libraries(RegionSelector_TextAnnotationEditor PRIVATE snaptray_ui Qt6::Test)
add_test(NAME RegionSelector_TextAnnotationEditor COMMAND RegionSelector_TextAnnotationEditor)
set_tests_properties(RegionSelector_TextAnnotationEditor PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(RegionSelector_SelectionStateManager RegionSelector/tst_SelectionStateManager.cpp)
target_link_libraries(RegionSelector_SelectionStateManager PRIVATE snaptray_ui Qt6::Test)
add_test(NAME RegionSelector_SelectionStateManager COMMAND RegionSelector_SelectionStateManager)
set_tests_properties(RegionSelector_SelectionStateManager PROPERTIES TIMEOUT 60 LABELS "unit")

# ============================================================================
# Encoding Tests (link snaptray_platform)
# ============================================================================

add_executable(Encoding_NativeGifEncoder Encoding/tst_NativeGifEncoder.cpp)
target_link_libraries(Encoding_NativeGifEncoder PRIVATE snaptray_platform Qt6::Test)
add_test(NAME Encoding_NativeGifEncoder COMMAND Encoding_NativeGifEncoder)
set_tests_properties(Encoding_NativeGifEncoder PROPERTIES TIMEOUT 120 LABELS "integration;slow")

add_executable(Encoding_EncoderFactory Encoding/tst_EncoderFactory.cpp)
target_link_libraries(Encoding_EncoderFactory PRIVATE snaptray_platform Qt6::Test)
add_test(NAME Encoding_EncoderFactory COMMAND Encoding_EncoderFactory)
set_tests_properties(Encoding_EncoderFactory PROPERTIES TIMEOUT 120 LABELS "integration;slow")

# ============================================================================
# Detection Tests (link snaptray_algorithms)
# ============================================================================

add_executable(Detection_FaceDetector Detection/tst_FaceDetector.cpp)
target_link_libraries(Detection_FaceDetector PRIVATE snaptray_algorithms Qt6::Widgets Qt6::Test)
# Add resources for cascade file
target_sources(Detection_FaceDetector PRIVATE ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
add_test(NAME Detection_FaceDetector COMMAND Detection_FaceDetector)
set_tests_properties(Detection_FaceDetector PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Detection_AutoBlurManager Detection/tst_AutoBlurManager.cpp)
target_link_libraries(Detection_AutoBlurManager PRIVATE snaptray_algorithms Qt6::Widgets Qt6::Test)
target_sources(Detection_AutoBlurManager PRIVATE ${CMAKE_SOURCE_DIR}/resources/resources.qrc)
add_test(NAME Detection_AutoBlurManager COMMAND Detection_AutoBlurManager)
set_tests_properties(Detection_AutoBlurManager PROPERTIES TIMEOUT 60 LABELS "unit")

# ============================================================================
# Utils Tests (link snaptray_core)
# ============================================================================

add_executable(Utils_CoordinateHelper Utils/tst_CoordinateHelper.cpp)
target_link_libraries(Utils_CoordinateHelper PRIVATE snaptray_core Qt6::Widgets Qt6::Test)
add_test(NAME Utils_CoordinateHelper COMMAND Utils_CoordinateHelper)
set_tests_properties(Utils_CoordinateHelper PROPERTIES TIMEOUT 60 LABELS "unit")

# ============================================================================
# Settings Tests (link snaptray_ui for full settings dependencies)
# ============================================================================

add_executable(Settings_AnnotationSettingsManager Settings/tst_AnnotationSettingsManager.cpp)
target_link_libraries(Settings_AnnotationSettingsManager PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Settings_AnnotationSettingsManager COMMAND Settings_AnnotationSettingsManager)
set_tests_properties(Settings_AnnotationSettingsManager PROPERTIES TIMEOUT 60 LABELS "unit")

# ============================================================================
# RecordingManager Tests
# ============================================================================
# Re-enabled: Recording components moved to snaptray_ui library for testability
# Mock classes available in tests/mocks/ for advanced testing scenarios

if(TRUE)  # Enabled

add_executable(RecordingManager_StateMachine RecordingManager/tst_StateMachine.cpp)
target_link_libraries(RecordingManager_StateMachine PRIVATE snaptray_ui snaptray_platform Qt6::Test)
add_test(NAME RecordingManager_StateMachine COMMAND RecordingManager_StateMachine)
set_tests_properties(RecordingManager_StateMachine PROPERTIES TIMEOUT 60 LABELS "integration")

add_executable(RecordingManager_Lifecycle RecordingManager/tst_Lifecycle.cpp)
target_link_libraries(RecordingManager_Lifecycle PRIVATE snaptray_ui snaptray_platform Qt6::Test)
add_test(NAME RecordingManager_Lifecycle COMMAND RecordingManager_Lifecycle)
set_tests_properties(RecordingManager_Lifecycle PROPERTIES TIMEOUT 60 LABELS "integration")

add_executable(RecordingManager_AudioIntegration RecordingManager/tst_AudioIntegration.cpp)
target_link_libraries(RecordingManager_AudioIntegration PRIVATE snaptray_ui snaptray_platform Qt6::Test)
add_test(NAME RecordingManager_AudioIntegration COMMAND RecordingManager_AudioIntegration)
set_tests_properties(RecordingManager_AudioIntegration PROPERTIES TIMEOUT 60 LABELS "integration")

add_executable(RecordingManager_InitTask RecordingManager/tst_InitTask.cpp)
target_link_libraries(RecordingManager_InitTask PRIVATE snaptray_ui snaptray_platform Qt6::Test)
add_test(NAME RecordingManager_InitTask COMMAND RecordingManager_InitTask)
set_tests_properties(RecordingManager_InitTask PROPERTIES TIMEOUT 60 LABELS "integration")

endif()  # End of temporarily disabled RecordingManager tests

# ============================================================================
# CTest Label Summary
# ============================================================================
# Labels:
#   unit        - Fast unit tests (most tests)
#   integration - Tests requiring platform features or external resources
#   slow        - Tests with longer timeouts (120s+)
#
# Run by label:
#   ctest -L unit           # Run all unit tests
#   ctest -L integration    # Run integration tests
#   ctest -L slow           # Run slow tests
#   ctest -LE slow          # Exclude slow tests
# ============================================================================
