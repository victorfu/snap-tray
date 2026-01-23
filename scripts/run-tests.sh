#!/bin/bash
# Run all tests for SnapTray

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

cd "$PROJECT_DIR"

# Configure if needed
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Configuring project..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
fi

# Build all test targets
echo "Building tests..."
cmake --build build --target \
    ToolOptionsPanel_State ToolOptionsPanel_Signals ToolOptionsPanel_HitTest ToolOptionsPanel_Events \
    PinWindow_Transform PinWindow_Resize PinWindow_ClickThrough \
    RegionSelector_MagnifierPanel RegionSelector_UpdateThrottler RegionSelector_TextAnnotationEditor RegionSelector_SelectionStateManager \
    Encoding_NativeGifEncoder Encoding_EncoderFactory \
    Detection_FaceDetector Detection_AutoBlurManager \
    Utils_CoordinateHelper \
    Settings_AnnotationSettingsManager Settings_PinWindowSettingsManager \
    Annotations_PencilStroke Annotations_ShapeAnnotation \
    Tools_ToolRegistry \
    Video_VideoTimeline \
    RecordingManager_StateMachine RecordingManager_Lifecycle RecordingManager_AudioIntegration RecordingManager_InitTask

# Run tests
echo ""
echo "Running tests..."
cd build && ctest --output-on-failure
