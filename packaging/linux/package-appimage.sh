#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="$PROJECT_DIR/release-linux"
APPDIR="$PROJECT_DIR/build/AppDir"
DIST_DIR="$PROJECT_DIR/dist"
TOOLS_DIR="$PROJECT_DIR/build/appimage-tools"

VERSION="$(grep "project(SnapTray VERSION" "$PROJECT_DIR/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/')"

if [ -z "$VERSION" ]; then
  echo "Failed to parse SnapTray version from CMakeLists.txt." >&2
  exit 1
fi

mkdir -p "$DIST_DIR" "$TOOLS_DIR"
rm -rf "$APPDIR"

CMAKE_ARGS=(
  -S "$PROJECT_DIR"
  -B "$BUILD_DIR"
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
)

if [ -n "${QT_ROOT_DIR:-}" ]; then
  CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=$QT_ROOT_DIR")
fi

cmake "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" --target SnapTray --parallel

SNAPTRAY_BINARY="$BUILD_DIR/bin/SnapTray"
if [ ! -x "$SNAPTRAY_BINARY" ]; then
  SNAPTRAY_BINARY="$BUILD_DIR/SnapTray"
fi

if [ ! -x "$SNAPTRAY_BINARY" ]; then
  echo "Failed to locate built SnapTray executable in $BUILD_DIR." >&2
  exit 1
fi

mkdir -p \
  "$APPDIR/usr/bin" \
  "$APPDIR/usr/share/applications" \
  "$APPDIR/usr/share/icons/hicolor/256x256/apps"
cp "$SNAPTRAY_BINARY" "$APPDIR/usr/bin/SnapTray"
cp "$SCRIPT_DIR/SnapTray.desktop" "$APPDIR/usr/share/applications/SnapTray.desktop"
cp "$PROJECT_DIR/resources/icons/snaptray.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/snaptray.png"

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"

if [ ! -x "$LINUXDEPLOY" ]; then
  curl -L \
    -o "$LINUXDEPLOY" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
  chmod +x "$LINUXDEPLOY"
fi

if [ ! -x "$LINUXDEPLOY_QT" ]; then
  curl -L \
    -o "$LINUXDEPLOY_QT" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
  chmod +x "$LINUXDEPLOY_QT"
fi

export QML_SOURCES_PATHS="$PROJECT_DIR/src/qml"
export EXTRA_QT_PLUGINS="imageformats;platforms;platformthemes;xcbglintegrations"
export OUTPUT="SnapTray-$VERSION-x86_64.AppImage"

cd "$PROJECT_DIR"
"$LINUXDEPLOY" \
  --appdir "$APPDIR" \
  --desktop-file "$APPDIR/usr/share/applications/SnapTray.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/snaptray.png" \
  --executable "$APPDIR/usr/bin/SnapTray" \
  --plugin qt \
  --output appimage

mv "$PROJECT_DIR/$OUTPUT" "$DIST_DIR/$OUTPUT"

VERSION_OUTPUT_FILE="$(mktemp)"
trap 'rm -f "$VERSION_OUTPUT_FILE"' EXIT
QT_QPA_PLATFORM=offscreen "$DIST_DIR/$OUTPUT" --version >"$VERSION_OUTPUT_FILE"
grep -q "SnapTray version" "$VERSION_OUTPUT_FILE"

echo "AppImage complete: $DIST_DIR/$OUTPUT"
