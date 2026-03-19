#!/bin/bash
set -e

# SnapTray macOS Packaging Script
# Creates a DMG with the application bundle
#
# Usage:
#   SPARKLE_FRAMEWORK="/path/to/Sparkle.framework" \
#   SNAPTRAY_SPARKLE_FEED_URL="https://example.com/appcast.xml" \
#   SNAPTRAY_SPARKLE_PUBLIC_KEY="..." \
#   ./package.sh
#
# Optional environment variables:
#   CODESIGN_IDENTITY     - Developer ID for code signing
#   NOTARIZE_KEYCHAIN_PROFILE - notarytool keychain profile name (recommended)
#   NOTARIZE_APPLE_ID     - Apple ID for notarization
#   NOTARIZE_TEAM_ID      - Team ID for notarization
#   NOTARIZE_PASSWORD     - App-specific password for notarization
#   SPARKLE_FRAMEWORK     - Absolute path to Sparkle.framework
#   SNAPTRAY_SPARKLE_FEED_URL - Sparkle appcast URL compiled into the app
#   SNAPTRAY_SPARKLE_PUBLIC_KEY - Sparkle EdDSA public key compiled into the app

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/build"
OUTPUT_DIR="${PROJECT_ROOT}/dist"
QML_SOURCE_DIR="${PROJECT_ROOT}/src/qml"
QML_IMPORT_DIR="${BUILD_DIR}"

codesign_runtime() {
    local target="$1"
    shift

    if [ -n "$CODESIGN_IDENTITY" ]; then
        codesign --force --sign "$CODESIGN_IDENTITY" \
            --timestamp \
            --options runtime \
            "$@" \
            "$target"
    else
        codesign --force --sign - \
            --options runtime \
            "$@" \
            "$target"
    fi
}

codesign_nested() {
    local target="$1"
    shift

    if [ -n "$CODESIGN_IDENTITY" ]; then
        codesign --force --sign "$CODESIGN_IDENTITY" \
            --timestamp \
            "$@" \
            "$target"
    else
        codesign --force --sign - \
            "$@" \
            "$target"
    fi
}

notarize_artifact() {
    local artifact="$1"
    local description="$2"

    if [ -z "$CODESIGN_IDENTITY" ]; then
        echo -e "${YELLOW}Skipping ${description} notarization (CODESIGN_IDENTITY not set)${NC}"
        return 1
    elif [ -n "$NOTARIZE_KEYCHAIN_PROFILE" ]; then
        echo -e "${YELLOW}Submitting ${description} for notarization (keychain profile: $NOTARIZE_KEYCHAIN_PROFILE)...${NC}"
        if ! xcrun notarytool submit "$artifact" \
            --keychain-profile "$NOTARIZE_KEYCHAIN_PROFILE" \
            --wait; then
            echo -e "${RED}Error: Failed to notarize ${description}.${NC}"
            exit 1
        fi
        return 0
    elif [ -n "$NOTARIZE_APPLE_ID" ] && [ -n "$NOTARIZE_TEAM_ID" ] && [ -n "$NOTARIZE_PASSWORD" ]; then
        echo -e "${YELLOW}Submitting ${description} for notarization (Apple ID credentials)...${NC}"
        if ! xcrun notarytool submit "$artifact" \
            --apple-id "$NOTARIZE_APPLE_ID" \
            --team-id "$NOTARIZE_TEAM_ID" \
            --password "$NOTARIZE_PASSWORD" \
            --wait; then
            echo -e "${RED}Error: Failed to notarize ${description}.${NC}"
            exit 1
        fi
        return 0
    else
        echo -e "${YELLOW}Skipping ${description} notarization (credentials not set)${NC}"
        return 1
    fi
}

# Extract version from CMakeLists.txt
VERSION=$(grep "project(SnapTray VERSION" "$PROJECT_ROOT/CMakeLists.txt" | \
          sed -E 's/.*VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/')
APP_NAME="SnapTray"
DMG_NAME="${APP_NAME}-${VERSION}-macOS"
DMG_PATH="${OUTPUT_DIR}/${DMG_NAME}.dmg"
APP_NOTARIZE_ARCHIVE="${OUTPUT_DIR}/${APP_NAME}-${VERSION}-macOS-notarize.zip"
SPARKLE_ARCHIVE_PATH="${OUTPUT_DIR}/${APP_NAME}-${VERSION}-macOS-sparkle.zip"

# Colors for output
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
NC='\033[0m' # No Color

echo -e "${GREEN}=== SnapTray macOS Packager ===${NC}"
echo "Version: $VERSION"
echo "Build directory: $BUILD_DIR"
echo "Output directory: $OUTPUT_DIR"
echo ""

if [ -z "$SNAPTRAY_SPARKLE_FEED_URL" ]; then
    echo -e "${RED}Error: SNAPTRAY_SPARKLE_FEED_URL is required for direct-download packaging${NC}"
    exit 1
fi

if [ -z "$SNAPTRAY_SPARKLE_PUBLIC_KEY" ]; then
    echo -e "${RED}Error: SNAPTRAY_SPARKLE_PUBLIC_KEY is required for direct-download packaging${NC}"
    exit 1
fi

if [ -z "$SPARKLE_FRAMEWORK" ]; then
    if [ -d "${PROJECT_ROOT}/third_party/Sparkle/Sparkle.framework" ]; then
        SPARKLE_FRAMEWORK="${PROJECT_ROOT}/third_party/Sparkle/Sparkle.framework"
    elif [ -d "${PROJECT_ROOT}/vendor/Sparkle/Sparkle.framework" ]; then
        SPARKLE_FRAMEWORK="${PROJECT_ROOT}/vendor/Sparkle/Sparkle.framework"
    fi
fi

if [ -z "$SPARKLE_FRAMEWORK" ] || [ ! -d "$SPARKLE_FRAMEWORK" ]; then
    echo -e "${RED}Error: SPARKLE_FRAMEWORK must point to a valid Sparkle.framework${NC}"
    exit 1
fi

# Check for Qt
QT_PREFIX=$(brew --prefix qt 2>/dev/null || echo "")
if [ -z "$QT_PREFIX" ] || [ ! -d "$QT_PREFIX" ]; then
    echo -e "${RED}Error: Qt not found. Please install with: brew install qt${NC}"
    exit 1
fi
echo "Qt path: $QT_PREFIX"

# Step 1: Build Release
echo ""
echo -e "${YELLOW}[1/8] Building release...${NC}"

# Check for ccache (improves rebuild times significantly)
CCACHE_ARGS=""
if command -v ccache &> /dev/null; then
    echo "Using ccache for faster builds"
    CCACHE_ARGS="-DCMAKE_CXX_COMPILER_LAUNCHER=ccache -DCMAKE_C_COMPILER_LAUNCHER=ccache"
fi

cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX" \
    -DSNAPTRAY_SPARKLE_FEED_URL="$SNAPTRAY_SPARKLE_FEED_URL" \
    -DSNAPTRAY_SPARKLE_PUBLIC_KEY="$SNAPTRAY_SPARKLE_PUBLIC_KEY" \
    $CCACHE_ARGS
cmake --build "$BUILD_DIR" --config Release --parallel

# App is built in bin/ subdirectory
APP_PATH="$BUILD_DIR/bin/${APP_NAME}.app"

# Check if app was built
if [ ! -d "$APP_PATH" ]; then
    echo -e "${RED}Error: Build failed - ${APP_NAME}.app not found${NC}"
    exit 1
fi

# Step 2: Run macdeployqt
echo ""
echo -e "${YELLOW}[2/8] Running macdeployqt...${NC}"
"$QT_PREFIX/bin/macdeployqt" "$APP_PATH" \
    -qmldir="$QML_SOURCE_DIR" \
    -qmlimport="$QML_IMPORT_DIR" \
    -verbose=1

echo "Removing optional Qt modules pulled in by Homebrew macdeployqt..."
OPTIONAL_QML_PATHS=(
    "$APP_PATH/Contents/Resources/qml/Qt3D"
    "$APP_PATH/Contents/Resources/qml/QtPdf"
    "$APP_PATH/Contents/Resources/qml/QtQuick/Scene3D"
    "$APP_PATH/Contents/Resources/qml/QtQuick/Timeline"
    "$APP_PATH/Contents/Resources/qml/QtQuick/VirtualKeyboard"
    "$APP_PATH/Contents/Resources/qml/QtQuick3D"
    "$APP_PATH/Contents/Resources/qml/QtQml/StateMachine"
)

for path in "${OPTIONAL_QML_PATHS[@]}"; do
    if [ -e "$path" ]; then
        echo "  Removing $path"
        rm -rf "$path"
    fi
done

OPTIONAL_PLUGIN_GLOBS=(
    "$APP_PATH/Contents/PlugIns/quick/libqtvkb*.dylib"
    "$APP_PATH/Contents/PlugIns/quick/libqthunspellinputmethodplugin.dylib"
)

for pattern in "${OPTIONAL_PLUGIN_GLOBS[@]}"; do
    for plugin in $pattern; do
        if [ -e "$plugin" ]; then
            echo "  Removing $plugin"
            rm -f "$plugin"
        fi
    done
done

# Fix brotli dependencies not handled by macdeployqt (QTBUG-100686)
echo "Fixing brotli dependencies..."
FRAMEWORKS_DIR="$APP_PATH/Contents/Frameworks"

# Find brotli's lib directory (installed separately via Homebrew, not part of Qt)
BROTLI_PREFIX=$(brew --prefix brotli 2>/dev/null || echo "")
BROTLI_LIB_DIR="$BROTLI_PREFIX/lib"

# List of brotli libraries that need to be copied
BROTLI_LIBS=(
    "libbrotlicommon.1.dylib"
    "libbrotlidec.1.dylib"
    "libbrotlienc.1.dylib"
)

# Copy brotli libraries if not already present
for lib in "${BROTLI_LIBS[@]}"; do
    src_lib="$BROTLI_LIB_DIR/$lib"
    dst_lib="$FRAMEWORKS_DIR/$lib"

    if [ -f "$src_lib" ] && [ ! -f "$dst_lib" ]; then
        echo "  Copying $lib from $BROTLI_LIB_DIR..."
        cp "$src_lib" "$dst_lib"
        chmod 644 "$dst_lib"
    fi
done

# Fix the install names for all brotli libraries
for lib in "${BROTLI_LIBS[@]}"; do
    lib_path="$FRAMEWORKS_DIR/$lib"
    [ -f "$lib_path" ] || continue

    echo "  Fixing $lib install names..."

    # Fix the library's own ID to use @executable_path
    install_name_tool -id \
        "@executable_path/../Frameworks/$lib" \
        "$lib_path" 2>/dev/null || true

    # Fix references to libbrotlicommon (used by libbrotlidec and libbrotlienc)
    # Try multiple possible source paths
    install_name_tool -change \
        "@rpath/libbrotlicommon.1.dylib" \
        "@executable_path/../Frameworks/libbrotlicommon.1.dylib" \
        "$lib_path" 2>/dev/null || true

    install_name_tool -change \
        "$BROTLI_LIB_DIR/libbrotlicommon.1.dylib" \
        "@executable_path/../Frameworks/libbrotlicommon.1.dylib" \
        "$lib_path" 2>/dev/null || true

    install_name_tool -change \
        "/opt/homebrew/opt/brotli/lib/libbrotlicommon.1.dylib" \
        "@executable_path/../Frameworks/libbrotlicommon.1.dylib" \
        "$lib_path" 2>/dev/null || true

    install_name_tool -change \
        "/usr/local/opt/brotli/lib/libbrotlicommon.1.dylib" \
        "@executable_path/../Frameworks/libbrotlicommon.1.dylib" \
        "$lib_path" 2>/dev/null || true
done

echo "Brotli dependencies fixed."

echo "Re-signing modified Brotli dylibs..."
for lib in "${BROTLI_LIBS[@]}"; do
    lib_path="$FRAMEWORKS_DIR/$lib"
    [ -f "$lib_path" ] || continue
    codesign_nested "$lib_path"
done

# Step 3: Bundle Sparkle
echo ""
echo -e "${YELLOW}[3/8] Bundling Sparkle.framework...${NC}"
SPARKLE_DEST="$APP_PATH/Contents/Frameworks/Sparkle.framework"
rm -rf "$SPARKLE_DEST"
cp -R "$SPARKLE_FRAMEWORK" "$SPARKLE_DEST"

# Step 4: Code signing
echo ""
echo -e "${YELLOW}[4/8] Signing application...${NC}"
echo "Removing quarantine attributes from app bundle..."
xattr -cr "$APP_PATH"
if [ -n "$CODESIGN_IDENTITY" ]; then
    echo "Using Developer ID: $CODESIGN_IDENTITY"
else
    echo "Using ad-hoc signing (no Developer ID configured)"
fi

# Sparkle helpers need explicit signing when packaging outside Xcode's archive/export flow.
SPARKLE_CURRENT="$SPARKLE_DEST/Versions/Current"
if [ -d "$SPARKLE_CURRENT/XPCServices/Installer.xpc" ]; then
    codesign_runtime "$SPARKLE_CURRENT/XPCServices/Installer.xpc"
fi

if [ -d "$SPARKLE_CURRENT/XPCServices/Downloader.xpc" ]; then
    codesign_runtime "$SPARKLE_CURRENT/XPCServices/Downloader.xpc" \
        --preserve-metadata=entitlements
fi

if [ -e "$SPARKLE_CURRENT/Autoupdate" ]; then
    codesign_runtime "$SPARKLE_CURRENT/Autoupdate"
fi

if [ -d "$SPARKLE_CURRENT/Updater.app" ]; then
    codesign_runtime "$SPARKLE_CURRENT/Updater.app"
fi

codesign_runtime "$SPARKLE_DEST"
codesign_runtime "$APP_PATH" --entitlements "$SCRIPT_DIR/entitlements.plist"

echo "Verifying signature..."
codesign --verify --deep --strict --verbose=2 "$APP_PATH"
echo -e "${GREEN}Signature verified${NC}"

# Step 5: Notarize app bundle before creating distributable archives
echo ""
mkdir -p "$OUTPUT_DIR"
echo -e "${YELLOW}[5/8] Preparing notarized app bundle...${NC}"
rm -f "$APP_NOTARIZE_ARCHIVE"
ditto -c -k --sequesterRsrc --keepParent "$APP_PATH" "$APP_NOTARIZE_ARCHIVE"

APP_NOTARIZED=0
if notarize_artifact "$APP_NOTARIZE_ARCHIVE" "app bundle archive"; then
    echo "Stapling notarization ticket to app bundle..."
    xcrun stapler staple "$APP_PATH"
    echo "Validating stapled app bundle..."
    xcrun stapler validate "$APP_PATH"
    APP_NOTARIZED=1
fi
rm -f "$APP_NOTARIZE_ARCHIVE"

# Step 6: Create Sparkle archive
echo ""
echo -e "${YELLOW}[6/8] Creating Sparkle archive...${NC}"
rm -f "$SPARKLE_ARCHIVE_PATH"
ditto -c -k --sequesterRsrc --keepParent "$APP_PATH" "$SPARKLE_ARCHIVE_PATH"

# Step 7: Create DMG
echo ""
echo -e "${YELLOW}[7/8] Creating DMG...${NC}"

# Remove existing DMG if present
rm -f "$DMG_PATH"

# Check if create-dmg is available (prettier DMG)
if command -v create-dmg &> /dev/null; then
    echo "Using create-dmg for prettier output..."
    create-dmg \
        --volname "$APP_NAME" \
        --window-pos 200 120 \
        --window-size 600 400 \
        --icon-size 100 \
        --icon "${APP_NAME}.app" 150 190 \
        --hide-extension "${APP_NAME}.app" \
        --app-drop-link 450 185 \
        "$DMG_PATH" \
        "$APP_PATH" 2>/dev/null || {
            # Fallback if create-dmg fails
            echo "create-dmg failed, falling back to hdiutil..."
            hdiutil create -volname "$APP_NAME" \
                -srcfolder "$APP_PATH" \
                -ov -format UDZO \
                "$DMG_PATH"
        }
else
    # Fallback to hdiutil
    echo "Using hdiutil (install create-dmg for prettier output: brew install create-dmg)"
    hdiutil create -volname "$APP_NAME" \
        -srcfolder "$APP_PATH" \
        -ov -format UDZO \
        "$DMG_PATH"
fi

# Step 8: Sign DMG (Developer ID only)
echo ""
if [ -n "$CODESIGN_IDENTITY" ]; then
    echo -e "${YELLOW}[8/8] Signing DMG...${NC}"
    codesign --force --sign "$CODESIGN_IDENTITY" --timestamp "$DMG_PATH"
    echo "Verifying DMG signature..."
    codesign --verify --verbose=2 "$DMG_PATH"
    echo -e "${GREEN}DMG signature verified${NC}"
else
    echo -e "${YELLOW}[8/8] Skipping DMG signing (CODESIGN_IDENTITY not set)${NC}"
fi

# Notarization (if credentials provided)
echo ""
DMG_NOTARIZED=0
if notarize_artifact "$DMG_PATH" "DMG"; then
    DMG_NOTARIZED=1
fi

if [ "$DMG_NOTARIZED" -eq 1 ]; then
    echo "Stapling notarization ticket..."
    xcrun stapler staple "$DMG_PATH"
    echo "Validating stapled ticket..."
    xcrun stapler validate "$DMG_PATH"
    echo -e "${GREEN}Notarization complete${NC}"
fi

# Done
echo ""
echo -e "${GREEN}=== Build Complete ===${NC}"
echo -e "Package: ${GREEN}$DMG_PATH${NC}"
echo -e "Sparkle Archive: ${GREEN}$SPARKLE_ARCHIVE_PATH${NC}"
echo ""
echo "To install:"
echo "  1. Open the DMG"
echo "  2. Drag SnapTray to Applications"
echo "  3. (Optional) Enable CLI from Settings > General"
echo ""
if [ -z "$CODESIGN_IDENTITY" ]; then
    echo -e "${YELLOW}Note: This build uses ad-hoc signing (no Apple Developer ID).${NC}"
    echo "If macOS blocks the app, users can run:"
    echo -e "  ${GREEN}xattr -cr /Applications/SnapTray.app${NC}"
    echo ""
    echo "Or: Right-click the app → Open → Click 'Open' in the dialog"
elif [ "$APP_NOTARIZED" -eq 1 ] && [ "$DMG_NOTARIZED" -eq 1 ]; then
    echo -e "${GREEN}The app bundle used for Sparkle updates and the DMG are both signed and notarized.${NC}"
else
    echo -e "${YELLOW}This DMG is signed but not notarized. Gatekeeper warnings may still appear.${NC}"
fi
