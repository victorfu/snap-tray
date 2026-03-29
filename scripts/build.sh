#!/bin/bash
# Build debug version of SnapTray (without running)

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
    local buildsystem_file="$build_dir/Makefile"
    local cached_type=""
    local cached_generator=""
    local desired_generator=""
    local needs_reset=0
    local -a cmake_args=(-S . -B "$build_name" -DCMAKE_BUILD_TYPE="$build_type")

    if [[ "$OSTYPE" == darwin* ]]; then
        desired_generator="Ninja"
        buildsystem_file="$build_dir/build.ninja"
        cmake_args=(-G "$desired_generator" "${cmake_args[@]}" -DCMAKE_PREFIX_PATH="$(brew --prefix qt)")
    fi

    if [ ! -f "$cache_file" ]; then
        echo "Configuring project ($build_type)..."
        cmake "${cmake_args[@]}"
        return
    fi

    cached_type="$(sed -n 's/^CMAKE_BUILD_TYPE:STRING=//p' "$cache_file" | head -n 1)"
    cached_generator="$(sed -n 's/^CMAKE_GENERATOR:INTERNAL=//p' "$cache_file" | head -n 1)"
    if { [ -n "$desired_generator" ] && [ "$cached_generator" != "$desired_generator" ]; } || \
       [ ! -f "$buildsystem_file" ]; then
        needs_reset=1
    fi

    if [ "$needs_reset" -eq 1 ]; then
        echo "Resetting build directory for a fresh $build_type configure..."
        rm -rf "$build_dir"
        cmake "${cmake_args[@]}"
        return
    fi

    if [ "$cached_type" != "$build_type" ]; then
        echo "Reconfiguring project ($build_type)..."
        cmake "${cmake_args[@]}"
    fi
}

configure_build_dir "$BUILD_DIR" "$(basename "$BUILD_DIR")" "Debug"

# Build only the app target
echo "Building SnapTray target..."
cmake --build "$BUILD_DIR" --target SnapTray

echo ""
echo "Build complete: $BUILD_DIR/bin/SnapTray-Debug.app"
