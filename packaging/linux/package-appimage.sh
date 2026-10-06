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

for tool in python3 xvfb-run xauth; do
  if ! command -v "$tool" >/dev/null 2>&1; then
    echo "AppImage startup checks require $tool. Install python3, xvfb, and xauth." >&2
    exit 1
  fi
done

mkdir -p "$DIST_DIR" "$TOOLS_DIR" "$TOOLS_DOWNLOADS_DIR"
rm -rf "$APPDIR"

CMAKE_ARGS=(
  -S "$PROJECT_DIR"
  -B "$BUILD_DIR"
  -G Ninja
  -DCMAKE_BUILD_TYPE=Release
  -DSNAPTRAY_ENABLE_LINUX_RECORDING=ON
)

if [ -n "${QT_ROOT_DIR:-}" ]; then
  CMAKE_ARGS+=("-DCMAKE_PREFIX_PATH=$QT_ROOT_DIR")
  export PATH="$QT_ROOT_DIR/bin:$PATH"
  export QMAKE="$QT_ROOT_DIR/bin/qmake"
fi

cmake "${CMAKE_ARGS[@]}"
cmake --build "$BUILD_DIR" --target SnapTray --parallel "${CMAKE_BUILD_PARALLEL_LEVEL:-4}"

# Build the isolated updater without importing upstream CMake options into SnapTray.
cmake -S "$PROJECT_DIR/src/update/appimage" -B "$BUILD_DIR/appimage-updater" -G Ninja \
  -DCMAKE_BUILD_TYPE=Release ${QT_ROOT_DIR:+"-DCMAKE_PREFIX_PATH=$QT_ROOT_DIR"}
cmake --build "$BUILD_DIR/appimage-updater" --target snaptray-appimage-updater --parallel
UPDATER="$BUILD_DIR/appimage-updater/snaptray-appimage-updater"

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
cp "$UPDATER" "$APPDIR/usr/bin/snaptray-appimage-updater"
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
# Qt 6.10's software scene graph is built into libQt6Quick, not a scenegraph
# plugin. The headless rendering check also needs the offscreen QPA plugin;
# linuxdeploy-plugin-qt deploys only xcb by default. EXTRA_QT_PLUGINS is a
# deprecated alias for Qt modules, not a list of plugin directories.
export EXTRA_PLATFORM_PLUGINS="${EXTRA_PLATFORM_PLUGINS:+${EXTRA_PLATFORM_PLUGINS};}libqoffscreen.so"
export OUTPUT="SnapTray-$VERSION-x86_64.AppImage"
export LDAI_OUTPUT="$OUTPUT"
export LDAI_UPDATE_INFORMATION="gh-releases-zsync|victorfu|snap-tray|latest|SnapTray-*-x86_64.AppImage.zsync"
export UPDATE_INFORMATION="$LDAI_UPDATE_INFORMATION"
if [ -n "${SNAPTRAY_APPIMAGE_SIGN_KEY:-}" ]; then
  export LDAI_SIGN=1 LDAI_SIGN_KEY="$SNAPTRAY_APPIMAGE_SIGN_KEY"
fi
if [ "${SNAPTRAY_REQUIRE_APPIMAGE_SIGNATURE:-0}" = 1 ] && [ -z "${SNAPTRAY_APPIMAGE_SIGN_KEY:-}" ]; then
  echo "Release AppImages require SNAPTRAY_APPIMAGE_SIGN_KEY." >&2
  exit 1
fi

cd "$PROJECT_DIR"
"$LINUXDEPLOY_RUNNER" \
  --appdir "$APPDIR" \
  --desktop-file "$APPDIR/usr/share/applications/SnapTray.desktop" \
  --icon-file "$APPDIR/usr/share/icons/hicolor/scalable/apps/snaptray.svg" \
  --executable "$APPDIR/usr/bin/SnapTray" \
  --executable "$APPDIR/usr/bin/snaptray-appimage-updater" \
  --plugin qt \
  --output appimage

mv "$PROJECT_DIR/$OUTPUT" "$DIST_DIR/$OUTPUT"
validate_appimage_header "$DIST_DIR/$OUTPUT"
test -s "$PROJECT_DIR/$OUTPUT.zsync"
mv "$PROJECT_DIR/$OUTPUT.zsync" "$DIST_DIR/$OUTPUT.zsync"
if [ -n "${SNAPTRAY_APPIMAGE_SIGN_KEY:-}" ]; then
  "$UPDATER" validate "$DIST_DIR/$OUTPUT"
fi
python3 "$SCRIPT_DIR/verify-update-artifacts.py" "$DIST_DIR/$OUTPUT"

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
# Catalogs read metadata from the AppDir root, including symlink targets.
for metadata in SnapTray.desktop snaptray.svg .DirIcon; do
  if [ ! -f "$SMOKE_APPDIR/squashfs-root/$metadata" ]; then
    echo "Missing root $metadata in AppImage." >&2
    exit 1
  fi
done
if [ ! -x "$SMOKE_APPDIR/squashfs-root/AppRun" ]; then
  echo "Missing executable AppRun in AppImage." >&2
  exit 1
fi

# Inspect the extracted artifact, not the build host's Qt installation.
for plugin in libqwebp.so libqtiff.so; do
  if [ -z "$(find "$SMOKE_APPDIR/squashfs-root/usr" -path "*/imageformats/$plugin" -type f -print -quit)" ]; then
    echo "Missing imageformats/$plugin in AppImage. Install Qt Image Formats (qtimageformats) for the packaging Qt SDK." >&2
    exit 1
  fi
done

# AppImages bundle the build distribution's media libraries as a matching ABI
# set. GPU drivers and the PulseAudio/PipeWire server remain host services.
for library in libavcodec libavformat libavutil libswscale libswresample libpulse; do
  if [ -z "$(find "$SMOKE_APPDIR/squashfs-root/usr/lib" -name "$library.so.*" -print -quit)" ]; then
    echo "Missing recording runtime library $library in AppImage." >&2
    exit 1
  fi
done

env -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH -u LD_PRELOAD \
  LD_LIBRARY_PATH="$SMOKE_APPDIR/squashfs-root/usr/lib" \
  QT_QPA_PLATFORM=offscreen SNAPTRAY_FFMPEG_ENCODER=software \
  timeout 30s "$SMOKE_APPDIR/squashfs-root/AppRun" --internal-recording-smoke-check

QT_QPA_PLATFORM=offscreen "$SMOKE_APPDIR/squashfs-root/AppRun" --version >"$VERSION_OUTPUT_FILE"
grep -q "SnapTray version" "$VERSION_OUTPUT_FILE"

# Use the extracted artifact's libraries, plugins and QML imports, without Qt
# SDK path overrides or a backend override masking the application's policy.
env -u QT_PLUGIN_PATH -u QT_QPA_PLATFORM_PLUGIN_PATH \
  -u QML_IMPORT_PATH -u QML2_IMPORT_PATH \
  -u QT_QUICK_BACKEND -u QMLSCENE_DEVICE -u QSG_RHI_BACKEND \
  -u LD_PRELOAD \
  LD_LIBRARY_PATH="$SMOKE_APPDIR/squashfs-root/usr/lib" \
  QT_QPA_PLATFORM=offscreen \
  timeout 15s "$SMOKE_APPDIR/squashfs-root/AppRun" --internal-qt-quick-smoke-check

# Test normal startup too: --version and the rendering probe bypass the Linux
# runtime guard, while AppImageHub starts under Xvfb without session metadata.
LD_LIBRARY_PATH="$SMOKE_APPDIR/squashfs-root/usr/lib" \
  xvfb-run -a python3 "$PROJECT_DIR/tests/Packaging/tst_LinuxX11Startup.py" \
  "$SMOKE_APPDIR/squashfs-root/AppRun"

echo "AppImage complete: $DIST_DIR/$OUTPUT"
