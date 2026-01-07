#!/bin/bash
# Build release version and run SnapTray

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/release"
BIN_DIR="$BUILD_DIR/bin"
APP_PATH="$BIN_DIR/SnapTray.app"
EXE_PATH="$APP_PATH/Contents/MacOS/SnapTray"

cd "$PROJECT_DIR"

# Configure if needed
if [ ! -f "$BUILD_DIR/CMakeCache.txt" ]; then
    echo "Configuring project..."
    cmake -S . -B release -DCMAKE_BUILD_TYPE=Release -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
fi

# Build all targets
echo "Building all targets..."
cmake --build release

# Check if macdeployqt is needed (detect by checking for QtCore.framework)
if [ -d "$APP_PATH" ] && [ ! -d "$APP_PATH/Contents/Frameworks/QtCore.framework" ]; then
    echo ""
    echo "Qt dependencies not found. Running macdeployqt..."

    # Try to find Qt installation from CMakeCache
    QT_DIR=$(grep "Qt6_DIR:PATH=" "$BUILD_DIR/CMakeCache.txt" 2>/dev/null | cut -d'=' -f2)

    if [ -n "$QT_DIR" ]; then
        # Navigate from Qt6_DIR (lib/cmake/Qt6) to bin
        QT_BIN_DIR="$QT_DIR/../../../bin"
        if [ -f "$QT_BIN_DIR/macdeployqt" ]; then
            "$QT_BIN_DIR/macdeployqt" "$APP_PATH"
        else
            echo "Warning: macdeployqt not found at $QT_BIN_DIR"
            echo "Please run macdeployqt manually or set Qt path correctly."
        fi
    else
        # Fallback: try brew Qt path
        BREW_QT_BIN="$(brew --prefix qt)/bin"
        if [ -f "$BREW_QT_BIN/macdeployqt" ]; then
            "$BREW_QT_BIN/macdeployqt" "$APP_PATH"
        else
            echo "Warning: Could not find macdeployqt"
            echo "Please run macdeployqt manually before running."
        fi
    fi
fi

# Run (Release builds use SnapTray.app)
echo ""
echo "Launching SnapTray in foreground..."
"$EXE_PATH"
