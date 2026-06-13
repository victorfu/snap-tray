#!/usr/bin/env bash
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/../.." && pwd)"
source "$SCRIPT_DIR/appimage-helpers.sh"

BUILD_DIR="$PROJECT_DIR/release-linux"
APPDIR="$PROJECT_DIR/build/AppDir"
DIST_DIR="$PROJECT_DIR/dist"
TOOLS_DIR="$PROJECT_DIR/build/appimage-tools"
TOOLS_DOWNLOADS_DIR="$TOOLS_DIR/downloads"

VERSION="$(grep "project(SnapTray VERSION" "$PROJECT_DIR/CMakeLists.txt" | sed -E 's/.*VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/')"

if [ -z "$VERSION" ]; then
  echo "Failed to parse SnapTray version from CMakeLists.txt." >&2
  exit 1
fi

mkdir -p "$DIST_DIR" "$TOOLS_DIR" "$TOOLS_DOWNLOADS_DIR"
rm -rf "$APPDIR"

CMAKE_ARGS=(
  -S "$PROJECT_DIR"
  -B "$BUILD_DIR"
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
)

if [ -n "${QT_ROOT_DIR:-}" ]; then
  CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=$QT_ROOT_DIR")
  export PATH="$QT_ROOT_DIR/bin:$PATH"
  export QMAKE="$QT_ROOT_DIR/bin/qmake"
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
  "$APPDIR/usr/share/icons/hicolor/scalable/apps"
cp "$SNAPTRAY_BINARY" "$APPDIR/usr/bin/SnapTray"
cp "$SCRIPT_DIR/SnapTray.desktop" "$APPDIR/usr/share/applications/SnapTray.desktop"
cp "$PROJECT_DIR/resources/icons/snaptray.svg" "$APPDIR/usr/share/icons/hicolor/scalable/apps/snaptray.svg"

LINUXDEPLOY="$TOOLS_DIR/linuxdeploy-x86_64.AppImage"
LINUXDEPLOY_QT="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"
LINUXDEPLOY_QT_DOWNLOAD="$TOOLS_DOWNLOADS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage"
OLD_LINUXDEPLOY_QT_DOWNLOAD="$TOOLS_DIR/linuxdeploy-plugin-qt-x86_64.AppImage.download"

download_linuxdeploy_qt() {
  local target="$1"
  curl -L \
    -o "$target" \
    "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage"
  chmod +x "$target"
}

extract_appimage_tool() {
  local appimage="$1"
  local tool_name="$2"
  local extract_dir="$TOOLS_DIR/$tool_name-extracted"
  local wrapper="$TOOLS_DIR/$tool_name"

  local offset
  offset="$(appimage_squashfs_offset "$appimage")"
  rm -rf "$extract_dir"
  mkdir -p "$extract_dir"
  (
    cd "$extract_dir"
    unsquashfs -q -o "$offset" "$appimage" >/dev/null
  )

  cat >"$wrapper" <<EOF
#!/usr/bin/env bash
exec "$extract_dir/squashfs-root/AppRun" "\$@"
EOF
  chmod +x "$wrapper"
  echo "$wrapper"
}

if [ ! -x "$LINUXDEPLOY" ]; then
  curl -L \
    -o "$LINUXDEPLOY" \
    "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
  chmod +x "$LINUXDEPLOY"
fi

if [ ! -x "$LINUXDEPLOY_QT" ] && [ ! -x "$LINUXDEPLOY_QT_DOWNLOAD" ]; then
  download_linuxdeploy_qt "$LINUXDEPLOY_QT"
fi

if [ ! -x "$LINUXDEPLOY_QT_DOWNLOAD" ] && [ -x "$OLD_LINUXDEPLOY_QT_DOWNLOAD" ]; then
  mv "$OLD_LINUXDEPLOY_QT_DOWNLOAD" "$LINUXDEPLOY_QT_DOWNLOAD"
fi

LINUXDEPLOY_RUNNER="$LINUXDEPLOY"
if ! timeout 15s "$LINUXDEPLOY" --version >/dev/null 2>&1; then
  echo "linuxdeploy AppImage could not be mounted via FUSE; extracting tool AppImages." >&2
  LINUXDEPLOY_RUNNER="$(extract_appimage_tool "$LINUXDEPLOY" linuxdeploy)"
  LINUXDEPLOY_QT_SOURCE="$LINUXDEPLOY_QT"
  if ! appimage_squashfs_offset "$LINUXDEPLOY_QT_SOURCE" >/dev/null 2>&1; then
    if [ ! -x "$LINUXDEPLOY_QT_DOWNLOAD" ]; then
      download_linuxdeploy_qt "$LINUXDEPLOY_QT_DOWNLOAD"
    fi
    LINUXDEPLOY_QT_SOURCE="$LINUXDEPLOY_QT_DOWNLOAD"
  fi
  if [ "$LINUXDEPLOY_QT_SOURCE" = "$LINUXDEPLOY_QT" ]; then
    cp "$LINUXDEPLOY_QT" "$LINUXDEPLOY_QT_DOWNLOAD"
    LINUXDEPLOY_QT_SOURCE="$LINUXDEPLOY_QT_DOWNLOAD"
  fi
  LINUXDEPLOY_QT_RUNNER="$(extract_appimage_tool "$LINUXDEPLOY_QT_SOURCE" linuxdeploy-plugin-qt)"
  cat >"$LINUXDEPLOY_QT" <<EOF
#!/usr/bin/env bash
exec "$LINUXDEPLOY_QT_RUNNER" "\$@"
EOF
  chmod +x "$LINUXDEPLOY_QT"
  export PATH="$TOOLS_DIR:$PATH"
fi

export QML_SOURCES_PATHS="$PROJECT_DIR/src/qml"
export EXTRA_QT_PLUGINS="imageformats;platforms;platformthemes;xcbglintegrations"
export OUTPUT="SnapTray-$VERSION-x86_64.AppImage"

cd "$PROJECT_DIR"
"$LINUXDEPLOY_RUNNER" \
  --appdir "$APPDIR" \
  --desktop-file "$APPDIR/usr/share/applications/SnapTray.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/snaptray.svg" \
  --executable "$APPDIR/usr/bin/SnapTray" \
  --plugin qt \
  --output appimage

mv "$PROJECT_DIR/$OUTPUT" "$DIST_DIR/$OUTPUT"
mask_appimage_binfmt_magic "$DIST_DIR/$OUTPUT"
echo "Masked AppImage binfmt magic for direct launch on Ubuntu 22.04."

VERSION_OUTPUT_FILE="$(mktemp)"
trap 'rm -f "$VERSION_OUTPUT_FILE"' EXIT
if ! command -v unsquashfs >/dev/null 2>&1; then
  echo "AppImage smoke check requires unsquashfs." >&2
  echo "Install squashfs-tools, then rerun this script." >&2
  exit 1
fi

APPIMAGE_OFFSET="$(appimage_squashfs_offset "$DIST_DIR/$OUTPUT")"
SMOKE_APPDIR="$PROJECT_DIR/build/appimage-smoke"
rm -rf "$SMOKE_APPDIR"
mkdir -p "$SMOKE_APPDIR"
(
  cd "$SMOKE_APPDIR"
  unsquashfs -q -o "$APPIMAGE_OFFSET" "$DIST_DIR/$OUTPUT" >/dev/null
)
QT_QPA_PLATFORM=offscreen "$SMOKE_APPDIR/squashfs-root/AppRun" --version >"$VERSION_OUTPUT_FILE"
grep -q "SnapTray version" "$VERSION_OUTPUT_FILE"

echo "AppImage complete: $DIST_DIR/$OUTPUT"
