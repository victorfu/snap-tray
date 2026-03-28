#!/bin/bash
# Build debug version and run SnapTray

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

cd "$PROJECT_DIR"

configure_build_dir() {
    local build_dir="$1"
    local build_name="$2"
    local build_type="$3"
    local cache_file="$build_dir/CMakeCache.txt"

    if [ ! -f "$cache_file" ]; then
        echo "Configuring project ($build_type)..."
        cmake -S . -B "$build_name" -DCMAKE_BUILD_TYPE="$build_type" -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
        return
    fi

    local cached_type
    cached_type="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$cache_file" | head -n 1)"
    if [ "$cached_type" != "$build_type" ]; then
        echo "Reconfiguring project ($build_type)..."
        cmake -S . -B "$build_name" -DCMAKE_BUILD_TYPE="$build_type"
    fi
}

configure_build_dir "$BUILD_DIR" "build" "Debug"

# Build only the app target
echo "Building SnapTray target..."
cmake --build build --target SnapTray

# Run (Debug builds use SnapTray-Debug.app)
echo ""
echo "Launching SnapTray in foreground..."
"$BUILD_DIR/bin/SnapTray-Debug.app/Contents/MacOS/SnapTray-Debug"
