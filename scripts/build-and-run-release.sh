#!/bin/bash
# Build release version and run SnapTray

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/release"

cd "$PROJECT_DIR"

# Configure if needed
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Configuring project..."
    cmake -S . -B release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
fi

# Build all targets
echo "Building all targets..."
cmake --build release

# Run (Release builds use SnapTray.app)
echo ""
echo "Launching SnapTray in foreground..."
"$BUILD_DIR/bin/SnapTray.app/Contents/MacOS/SnapTray"
