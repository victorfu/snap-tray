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

add_executable(PinWindow_PinWindowPlacement PinWindow/tst_PinWindowPlacement.cpp)
target_link_libraries(PinWindow_PinWindowPlacement PRIVATE snaptray_ui Qt6::Test)
add_test(NAME PinWindow_PinWindowPlacement COMMAND PinWindow_PinWindowPlacement)
set_tests_properties(PinWindow_PinWindowPlacement PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(PinWindow_RegionLayoutManager PinWindow/tst_RegionLayoutManager.cpp)
target_link_libraries(PinWindow_RegionLayoutManager PRIVATE snaptray_ui Qt6::Test)
add_test(NAME PinWindow_RegionLayoutManager COMMAND PinWindow_RegionLayoutManager)
set_tests_properties(PinWindow_RegionLayoutManager PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(PinWindow_PinHistoryStore PinWindow/tst_PinHistoryStore.cpp)
target_link_libraries(PinWindow_PinHistoryStore PRIVATE snaptray_ui Qt6::Test)
add_test(NAME PinWindow_PinHistoryStore COMMAND PinWindow_PinHistoryStore)
set_tests_properties(PinWindow_PinHistoryStore PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(PinWindow_PinMergeHelper PinWindow/tst_PinMergeHelper.cpp)
target_link_libraries(PinWindow_PinMergeHelper PRIVATE snaptray_ui Qt6::Test)
add_test(NAME PinWindow_PinMergeHelper COMMAND PinWindow_PinMergeHelper)
set_tests_properties(PinWindow_PinMergeHelper PROPERTIES TIMEOUT 60 LABELS "integration")

add_executable(PinWindow_TextToolFormatting PinWindow/tst_TextToolFormatting.cpp)
target_link_libraries(PinWindow_TextToolFormatting PRIVATE snaptray_ui Qt6::Test)
add_test(NAME PinWindow_TextToolFormatting COMMAND PinWindow_TextToolFormatting)
set_tests_properties(PinWindow_TextToolFormatting PROPERTIES TIMEOUT 60 LABELS "unit;integration")

add_executable(PinWindow_CropUndo PinWindow/tst_CropUndo.cpp)
target_link_libraries(PinWindow_CropUndo PRIVATE snaptray_ui Qt6::Test)
add_test(NAME PinWindow_CropUndo COMMAND PinWindow_CropUndo)
set_tests_properties(PinWindow_CropUndo PROPERTIES TIMEOUT 60 LABELS "unit;integration")

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

add_executable(RegionSelector_TransformationGizmo RegionSelector/tst_TransformationGizmo.cpp)
target_link_libraries(RegionSelector_TransformationGizmo PRIVATE snaptray_ui Qt6::Test)
add_test(NAME RegionSelector_TransformationGizmo COMMAND RegionSelector_TransformationGizmo)
set_tests_properties(RegionSelector_TransformationGizmo PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(RegionSelector_ShapeAnnotationEditor RegionSelector/tst_ShapeAnnotationEditor.cpp)
target_link_libraries(RegionSelector_ShapeAnnotationEditor PRIVATE snaptray_ui Qt6::Test)
add_test(NAME RegionSelector_ShapeAnnotationEditor COMMAND RegionSelector_ShapeAnnotationEditor)
set_tests_properties(RegionSelector_ShapeAnnotationEditor PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(RegionSelector_RegionSettingsHelper RegionSelector/tst_RegionSettingsHelper.cpp)
target_link_libraries(RegionSelector_RegionSettingsHelper PRIVATE snaptray_ui Qt6::Test)
add_test(NAME RegionSelector_RegionSettingsHelper COMMAND RegionSelector_RegionSettingsHelper)
set_tests_properties(RegionSelector_RegionSettingsHelper PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(RegionSelector_SelectionStateManager RegionSelector/tst_SelectionStateManager.cpp)
target_link_libraries(RegionSelector_SelectionStateManager PRIVATE snaptray_ui Qt6::Test)
add_test(NAME RegionSelector_SelectionStateManager COMMAND RegionSelector_SelectionStateManager)
set_tests_properties(RegionSelector_SelectionStateManager PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(RegionSelector_SelectionDirtyRegionPlanner RegionSelector/tst_SelectionDirtyRegionPlanner.cpp)
target_link_libraries(RegionSelector_SelectionDirtyRegionPlanner PRIVATE snaptray_ui Qt6::Test)
add_test(NAME RegionSelector_SelectionDirtyRegionPlanner COMMAND RegionSelector_SelectionDirtyRegionPlanner)
set_tests_properties(RegionSelector_SelectionDirtyRegionPlanner PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(RegionSelector_MultiRegionManagerReorder RegionSelector/tst_MultiRegionManagerReorder.cpp)
target_link_libraries(RegionSelector_MultiRegionManagerReorder PRIVATE snaptray_ui Qt6::Test)
add_test(NAME RegionSelector_MultiRegionManagerReorder COMMAND RegionSelector_MultiRegionManagerReorder)
set_tests_properties(RegionSelector_MultiRegionManagerReorder PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(RegionSelector_MultiRegionManagerDeleteReindex RegionSelector/tst_MultiRegionManagerDeleteReindex.cpp)
target_link_libraries(RegionSelector_MultiRegionManagerDeleteReindex PRIVATE snaptray_ui Qt6::Test)
add_test(NAME RegionSelector_MultiRegionManagerDeleteReindex COMMAND RegionSelector_MultiRegionManagerDeleteReindex)
set_tests_properties(RegionSelector_MultiRegionManagerDeleteReindex PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(RegionSelector_MultiRegionListPanel RegionSelector/tst_MultiRegionListPanel.cpp)
target_link_libraries(RegionSelector_MultiRegionListPanel PRIVATE snaptray_ui Qt6::Test)
add_test(NAME RegionSelector_MultiRegionListPanel COMMAND RegionSelector_MultiRegionListPanel)
set_tests_properties(RegionSelector_MultiRegionListPanel PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(RegionSelector_MultiRegionReplaceFlow RegionSelector/tst_MultiRegionReplaceFlow.cpp)
target_link_libraries(RegionSelector_MultiRegionReplaceFlow PRIVATE snaptray_ui Qt6::Test)
add_test(NAME RegionSelector_MultiRegionReplaceFlow COMMAND RegionSelector_MultiRegionReplaceFlow)
set_tests_properties(RegionSelector_MultiRegionReplaceFlow PROPERTIES TIMEOUT 60 LABELS "unit")

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

add_executable(Encoding_EncodingWorker Encoding/tst_EncodingWorker.cpp)
target_link_libraries(Encoding_EncodingWorker PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Encoding_EncodingWorker COMMAND Encoding_EncodingWorker)
set_tests_properties(Encoding_EncodingWorker PROPERTIES TIMEOUT 60 LABELS "unit")

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

add_executable(Detection_TableDetector Detection/tst_TableDetector.cpp)
target_link_libraries(Detection_TableDetector PRIVATE snaptray_algorithms Qt6::Test)
add_test(NAME Detection_TableDetector COMMAND Detection_TableDetector)
set_tests_properties(Detection_TableDetector PROPERTIES TIMEOUT 60 LABELS "unit")

if(APPLE)
    add_executable(Detection_WindowDetectorMacFilters Detection/tst_WindowDetectorMacFilters.cpp)
    target_include_directories(Detection_WindowDetectorMacFilters PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(Detection_WindowDetectorMacFilters PRIVATE Qt6::Test)
    add_test(NAME Detection_WindowDetectorMacFilters COMMAND Detection_WindowDetectorMacFilters)
    set_tests_properties(Detection_WindowDetectorMacFilters PROPERTIES TIMEOUT 60 LABELS "unit")
endif()

# ============================================================================
# Utils Tests (link snaptray_core)
# ============================================================================

add_executable(Utils_CoordinateHelper Utils/tst_CoordinateHelper.cpp)
target_link_libraries(Utils_CoordinateHelper PRIVATE snaptray_core Qt6::Widgets Qt6::Test)
add_test(NAME Utils_CoordinateHelper COMMAND Utils_CoordinateHelper)
set_tests_properties(Utils_CoordinateHelper PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Utils_FilenameTemplateEngine Utils/tst_FilenameTemplateEngine.cpp)
target_link_libraries(Utils_FilenameTemplateEngine PRIVATE snaptray_core Qt6::Test)
add_test(NAME Utils_FilenameTemplateEngine COMMAND Utils_FilenameTemplateEngine)
set_tests_properties(Utils_FilenameTemplateEngine PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Utils_ImageSaveUtils Utils/tst_ImageSaveUtils.cpp)
target_link_libraries(Utils_ImageSaveUtils PRIVATE snaptray_core Qt6::Test)
add_test(NAME Utils_ImageSaveUtils COMMAND Utils_ImageSaveUtils)
set_tests_properties(Utils_ImageSaveUtils PROPERTIES TIMEOUT 60 LABELS "unit")

# ============================================================================
# CLI Tests (link snaptray_platform for PlatformFeatures symbols)
# ============================================================================

add_executable(CLI_NumericArgumentValidation CLI/tst_NumericArgumentValidation.cpp)
target_link_libraries(CLI_NumericArgumentValidation PRIVATE snaptray_platform Qt6::Test)
add_test(NAME CLI_NumericArgumentValidation COMMAND CLI_NumericArgumentValidation)
set_tests_properties(CLI_NumericArgumentValidation PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(CLI_CaptureOutputHelper CLI/tst_CaptureOutputHelper.cpp)
target_link_libraries(CLI_CaptureOutputHelper PRIVATE snaptray_platform Qt6::Test)
add_test(NAME CLI_CaptureOutputHelper COMMAND CLI_CaptureOutputHelper)
set_tests_properties(CLI_CaptureOutputHelper PROPERTIES TIMEOUT 60 LABELS "unit")

if(WIN32)
    add_executable(CLI_WindowsPathEnv
        CLI/tst_WindowsPathEnv.cpp
        ${CMAKE_SOURCE_DIR}/src/platform/PathEnvUtils_win.cpp
    )
    target_include_directories(CLI_WindowsPathEnv PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(CLI_WindowsPathEnv PRIVATE Qt6::Core Qt6::Test)
    add_test(NAME CLI_WindowsPathEnv COMMAND CLI_WindowsPathEnv)
    set_tests_properties(CLI_WindowsPathEnv PROPERTIES TIMEOUT 60 LABELS "unit")
endif()

# ============================================================================
# IPC Tests
# ============================================================================

add_executable(IPC_IPCProtocol
    IPC/tst_IPCProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/cli/IPCProtocol.cpp
)
target_include_directories(IPC_IPCProtocol PRIVATE
    ${CMAKE_SOURCE_DIR}/include
    ${CMAKE_BINARY_DIR}/generated
)
target_link_libraries(IPC_IPCProtocol PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME IPC_IPCProtocol COMMAND IPC_IPCProtocol)
set_tests_properties(IPC_IPCProtocol PROPERTIES TIMEOUT 60 LABELS "unit;integration")

add_executable(IPC_SingleInstanceGuard
    IPC/tst_SingleInstanceGuard.cpp
    ${CMAKE_SOURCE_DIR}/src/SingleInstanceGuard.cpp
    ${CMAKE_SOURCE_DIR}/include/SingleInstanceGuard.h
)
target_include_directories(IPC_SingleInstanceGuard PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(IPC_SingleInstanceGuard PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME IPC_SingleInstanceGuard COMMAND IPC_SingleInstanceGuard)
set_tests_properties(IPC_SingleInstanceGuard PROPERTIES TIMEOUT 60 LABELS "unit;integration")

add_executable(IPC_SingleInstanceGuardProbe
    IPC/SingleInstanceGuardProbe.cpp
    ${CMAKE_SOURCE_DIR}/src/SingleInstanceGuard.cpp
    ${CMAKE_SOURCE_DIR}/include/SingleInstanceGuard.h
)
target_include_directories(IPC_SingleInstanceGuardProbe PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(IPC_SingleInstanceGuardProbe PRIVATE Qt6::Core Qt6::Network)

add_executable(IPC_SingleInstanceGuardRace
    IPC/tst_SingleInstanceGuardRace.cpp
)
add_dependencies(IPC_SingleInstanceGuardRace IPC_SingleInstanceGuardProbe)
target_include_directories(IPC_SingleInstanceGuardRace PRIVATE ${CMAKE_SOURCE_DIR}/include)
target_link_libraries(IPC_SingleInstanceGuardRace PRIVATE Qt6::Core Qt6::Network Qt6::Test)
add_test(NAME IPC_SingleInstanceGuardRace COMMAND IPC_SingleInstanceGuardRace)
set_tests_properties(IPC_SingleInstanceGuardRace PROPERTIES TIMEOUT 60 LABELS "integration")

# ============================================================================
# Settings Tests (link snaptray_ui for full settings dependencies)
# ============================================================================

add_executable(Settings_AnnotationSettingsManager Settings/tst_AnnotationSettingsManager.cpp)
target_link_libraries(Settings_AnnotationSettingsManager PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Settings_AnnotationSettingsManager COMMAND Settings_AnnotationSettingsManager)
set_tests_properties(Settings_AnnotationSettingsManager PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Settings_PinWindowSettingsManager Settings/tst_PinWindowSettingsManager.cpp)
target_link_libraries(Settings_PinWindowSettingsManager PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Settings_PinWindowSettingsManager COMMAND Settings_PinWindowSettingsManager)
set_tests_properties(Settings_PinWindowSettingsManager PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Settings_FileSettingsManager Settings/tst_FileSettingsManager.cpp)
target_link_libraries(Settings_FileSettingsManager PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Settings_FileSettingsManager COMMAND Settings_FileSettingsManager)
set_tests_properties(Settings_FileSettingsManager PROPERTIES TIMEOUT 60 LABELS "unit")

# ============================================================================
# Hotkey Tests (link snaptray_ui for HotkeyManager)
# ============================================================================

add_executable(Hotkey_HotkeyManager Hotkey/tst_HotkeyManager.cpp)
target_link_libraries(Hotkey_HotkeyManager PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Hotkey_HotkeyManager COMMAND Hotkey_HotkeyManager)
set_tests_properties(Hotkey_HotkeyManager PROPERTIES TIMEOUT 60 LABELS "unit")

# ============================================================================
# Annotations Tests (link snaptray_core for annotation classes)
# ============================================================================

add_executable(Annotations_ShapeAnnotation Annotations/tst_ShapeAnnotation.cpp)
target_link_libraries(Annotations_ShapeAnnotation PRIVATE snaptray_core Qt6::Widgets Qt6::Test)
add_test(NAME Annotations_ShapeAnnotation COMMAND Annotations_ShapeAnnotation)
set_tests_properties(Annotations_ShapeAnnotation PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Annotations_ArrowAnnotation Annotations/tst_ArrowAnnotation.cpp)
target_link_libraries(Annotations_ArrowAnnotation PRIVATE snaptray_core Qt6::Widgets Qt6::Test)
add_test(NAME Annotations_ArrowAnnotation COMMAND Annotations_ArrowAnnotation)
set_tests_properties(Annotations_ArrowAnnotation PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Annotations_MarkerStroke Annotations/tst_MarkerStroke.cpp)
target_link_libraries(Annotations_MarkerStroke PRIVATE snaptray_core Qt6::Widgets Qt6::Test)
add_test(NAME Annotations_MarkerStroke COMMAND Annotations_MarkerStroke)
set_tests_properties(Annotations_MarkerStroke PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Annotations_PencilStroke Annotations/tst_PencilStroke.cpp)
target_link_libraries(Annotations_PencilStroke PRIVATE snaptray_core Qt6::Widgets Qt6::Test)
add_test(NAME Annotations_PencilStroke COMMAND Annotations_PencilStroke)
set_tests_properties(Annotations_PencilStroke PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Annotations_PolylineAnnotation Annotations/tst_PolylineAnnotation.cpp)
target_link_libraries(Annotations_PolylineAnnotation PRIVATE snaptray_core Qt6::Widgets Qt6::Test)
add_test(NAME Annotations_PolylineAnnotation COMMAND Annotations_PolylineAnnotation)
set_tests_properties(Annotations_PolylineAnnotation PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Annotations_AnnotationLayer Annotations/tst_AnnotationLayer.cpp)
target_link_libraries(Annotations_AnnotationLayer PRIVATE snaptray_ui Qt6::Widgets Qt6::Test)
add_test(NAME Annotations_AnnotationLayer COMMAND Annotations_AnnotationLayer)
set_tests_properties(Annotations_AnnotationLayer PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Annotations_AnnotationContext Annotations/tst_AnnotationContext.cpp)
target_link_libraries(Annotations_AnnotationContext PRIVATE snaptray_ui Qt6::Widgets Qt6::Test)
add_test(NAME Annotations_AnnotationContext COMMAND Annotations_AnnotationContext)
set_tests_properties(Annotations_AnnotationContext PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Annotations_TextBoxAnnotation Annotations/tst_TextBoxAnnotation.cpp)
target_link_libraries(Annotations_TextBoxAnnotation PRIVATE snaptray_core Qt6::Widgets Qt6::Test)
add_test(NAME Annotations_TextBoxAnnotation COMMAND Annotations_TextBoxAnnotation)
set_tests_properties(Annotations_TextBoxAnnotation PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Annotations_StepBadgeAnnotation Annotations/tst_StepBadgeAnnotation.cpp)
target_link_libraries(Annotations_StepBadgeAnnotation PRIVATE snaptray_core Qt6::Widgets Qt6::Test)
add_test(NAME Annotations_StepBadgeAnnotation COMMAND Annotations_StepBadgeAnnotation)
set_tests_properties(Annotations_StepBadgeAnnotation PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Annotations_EmojiStickerAnnotation Annotations/tst_EmojiStickerAnnotation.cpp)
target_link_libraries(Annotations_EmojiStickerAnnotation PRIVATE snaptray_core Qt6::Widgets Qt6::Test)
add_test(NAME Annotations_EmojiStickerAnnotation COMMAND Annotations_EmojiStickerAnnotation)
set_tests_properties(Annotations_EmojiStickerAnnotation PROPERTIES TIMEOUT 60 LABELS "unit")

# ============================================================================
# Tools Tests (link snaptray_ui for tool system)
# ============================================================================

add_executable(Tools_ToolRegistry Tools/tst_ToolRegistry.cpp)
target_link_libraries(Tools_ToolRegistry PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Tools_ToolRegistry COMMAND Tools_ToolRegistry)
set_tests_properties(Tools_ToolRegistry PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Tools_ToolManagerEscape Tools/tst_ToolManagerEscape.cpp)
target_link_libraries(Tools_ToolManagerEscape PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Tools_ToolManagerEscape COMMAND Tools_ToolManagerEscape)
set_tests_properties(Tools_ToolManagerEscape PROPERTIES TIMEOUT 60 LABELS "unit")

# ============================================================================
# Tool Handler Tests (link snaptray_ui for tool handlers)
# ============================================================================

add_executable(Tools_PencilToolHandler Tools/handlers/tst_PencilToolHandler.cpp)
target_link_libraries(Tools_PencilToolHandler PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Tools_PencilToolHandler COMMAND Tools_PencilToolHandler)
set_tests_properties(Tools_PencilToolHandler PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Tools_ArrowToolHandler Tools/handlers/tst_ArrowToolHandler.cpp)
target_link_libraries(Tools_ArrowToolHandler PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Tools_ArrowToolHandler COMMAND Tools_ArrowToolHandler)
set_tests_properties(Tools_ArrowToolHandler PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Tools_ShapeToolHandler Tools/handlers/tst_ShapeToolHandler.cpp)
target_link_libraries(Tools_ShapeToolHandler PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Tools_ShapeToolHandler COMMAND Tools_ShapeToolHandler)
set_tests_properties(Tools_ShapeToolHandler PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Tools_EraserToolHandler Tools/handlers/tst_EraserToolHandler.cpp)
target_link_libraries(Tools_EraserToolHandler PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Tools_EraserToolHandler COMMAND Tools_EraserToolHandler)
set_tests_properties(Tools_EraserToolHandler PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Tools_TextToolHandler Tools/handlers/tst_TextToolHandler.cpp)
target_link_libraries(Tools_TextToolHandler PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Tools_TextToolHandler COMMAND Tools_TextToolHandler)
set_tests_properties(Tools_TextToolHandler PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Tools_CropToolHandler Tools/handlers/tst_CropToolHandler.cpp)
target_link_libraries(Tools_CropToolHandler PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Tools_CropToolHandler COMMAND Tools_CropToolHandler)
set_tests_properties(Tools_CropToolHandler PROPERTIES TIMEOUT 60 LABELS "unit")

# ============================================================================
# UI Section Tests (link snaptray_ui for UI components)
# ============================================================================

add_executable(UISections_ColorSection UISections/tst_ColorSection.cpp)
target_link_libraries(UISections_ColorSection PRIVATE snaptray_ui Qt6::Test)
add_test(NAME UISections_ColorSection COMMAND UISections_ColorSection)
set_tests_properties(UISections_ColorSection PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(UISections_WidthSection UISections/tst_WidthSection.cpp)
target_link_libraries(UISections_WidthSection PRIVATE snaptray_ui Qt6::Test)
add_test(NAME UISections_WidthSection COMMAND UISections_WidthSection)
set_tests_properties(UISections_WidthSection PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(UISections_ShapeSection UISections/tst_ShapeSection.cpp)
target_link_libraries(UISections_ShapeSection PRIVATE snaptray_ui Qt6::Test)
add_test(NAME UISections_ShapeSection COMMAND UISections_ShapeSection)
set_tests_properties(UISections_ShapeSection PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(UISections_ArrowStyleSection UISections/tst_ArrowStyleSection.cpp)
target_link_libraries(UISections_ArrowStyleSection PRIVATE snaptray_ui Qt6::Test)
add_test(NAME UISections_ArrowStyleSection COMMAND UISections_ArrowStyleSection)
set_tests_properties(UISections_ArrowStyleSection PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(UISections_SizeSection UISections/tst_SizeSection.cpp)
target_link_libraries(UISections_SizeSection PRIVATE snaptray_ui Qt6::Test)
add_test(NAME UISections_SizeSection COMMAND UISections_SizeSection)
set_tests_properties(UISections_SizeSection PROPERTIES TIMEOUT 60 LABELS "unit")

# ============================================================================
# Video Tests (link snaptray_ui for video components)
# ============================================================================

add_executable(Video_VideoTimeline Video/tst_VideoTimeline.cpp)
target_link_libraries(Video_VideoTimeline PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Video_VideoTimeline COMMAND Video_VideoTimeline)
set_tests_properties(Video_VideoTimeline PROPERTIES TIMEOUT 60 LABELS "unit")

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

add_executable(RecordingManager_InitTask RecordingManager/tst_InitTask.cpp)
target_link_libraries(RecordingManager_InitTask PRIVATE snaptray_ui snaptray_platform Qt6::Test)
add_test(NAME RecordingManager_InitTask COMMAND RecordingManager_InitTask)
set_tests_properties(RecordingManager_InitTask PROPERTIES TIMEOUT 60 LABELS "integration")

add_executable(RecordingManager_RegionSizing RecordingManager/tst_RegionSizing.cpp)
target_link_libraries(RecordingManager_RegionSizing PRIVATE snaptray_ui snaptray_platform Qt6::Test)
add_test(NAME RecordingManager_RegionSizing COMMAND RecordingManager_RegionSizing)
set_tests_properties(RecordingManager_RegionSizing PROPERTIES TIMEOUT 60 LABELS "unit")

if(WIN32)
add_executable(RecordingManager_DXGICaptureEngineThreadLifecycle
    RecordingManager/tst_DXGICaptureEngineThreadLifecycle_win.cpp
)
target_link_libraries(RecordingManager_DXGICaptureEngineThreadLifecycle PRIVATE
    snaptray_platform
    Qt6::Gui
    Qt6::Test
)
add_test(NAME RecordingManager_DXGICaptureEngineThreadLifecycle
    COMMAND RecordingManager_DXGICaptureEngineThreadLifecycle)
set_tests_properties(RecordingManager_DXGICaptureEngineThreadLifecycle
    PROPERTIES TIMEOUT 120 LABELS "integration")
endif()

endif()  # End of temporarily disabled RecordingManager tests

# ============================================================================
# Beautify Tests (link snaptray_core - settings and renderer live in core)
# ============================================================================

add_executable(Beautify_Settings Beautify/tst_BeautifySettings.cpp)
target_link_libraries(Beautify_Settings PRIVATE snaptray_core Qt6::Widgets Qt6::Test)
add_test(NAME Beautify_Settings COMMAND Beautify_Settings)
set_tests_properties(Beautify_Settings PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Beautify_Renderer Beautify/tst_BeautifyRenderer.cpp)
target_link_libraries(Beautify_Renderer PRIVATE snaptray_core Qt6::Widgets Qt6::Test)
add_test(NAME Beautify_Renderer COMMAND Beautify_Renderer)
set_tests_properties(Beautify_Renderer PROPERTIES TIMEOUT 60 LABELS "unit")

# ============================================================================
# Update Tests (link snaptray_ui for update components)
# ============================================================================

add_executable(Update_UpdateSettingsManager Update/tst_UpdateSettingsManager.cpp)
target_link_libraries(Update_UpdateSettingsManager PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Update_UpdateSettingsManager COMMAND Update_UpdateSettingsManager)
set_tests_properties(Update_UpdateSettingsManager PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Update_UpdateChecker Update/tst_UpdateChecker.cpp)
target_link_libraries(Update_UpdateChecker PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Update_UpdateChecker COMMAND Update_UpdateChecker)
set_tests_properties(Update_UpdateChecker PROPERTIES TIMEOUT 60 LABELS "unit")

add_executable(Update_InstallSourceDetector Update/tst_InstallSourceDetector.cpp)
target_link_libraries(Update_InstallSourceDetector PRIVATE snaptray_ui Qt6::Test)
add_test(NAME Update_InstallSourceDetector COMMAND Update_InstallSourceDetector)
set_tests_properties(Update_InstallSourceDetector PROPERTIES TIMEOUT 60 LABELS "unit")

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
