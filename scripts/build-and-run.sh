#!/bin/bash
# Build debug version and run SnapTray

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

cd "$PROJECT_DIR"

# Configure if needed
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Configuring project..."
    cmake -S . -B build -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
fi

# Build
echo "Building SnapTray..."
cmake --build build --target SnapTray

# Run
echo ""
echo "Launching SnapTray..."
open build/SnapTray.app
