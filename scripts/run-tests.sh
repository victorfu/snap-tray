#!/bin/bash
# Run all tests for SnapTray

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
BUILD_DIR="$PROJECT_DIR/build"

cd "$PROJECT_DIR"

# Configure if needed (check for Makefile which indicates successful configuration)
if [ ! -f "$BUILD_DIR/Makefile" ]; then
    echo "Configuring project..."
    cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH="$(brew --prefix qt)"
fi

# Build all targets (including tests)
echo "Building..."
cmake --build build

# Run tests
echo ""
echo "Running tests..."
cd build && ctest --output-on-failure
