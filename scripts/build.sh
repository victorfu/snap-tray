#!/bin/bash
# Build debug version of SnapTray (without running)

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

# Build all targets (including tests)
echo "Building all targets..."
cmake --build build

echo ""
echo "Build complete: $BUILD_DIR/bin/SnapTray-Debug.app"
