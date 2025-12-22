#!/bin/bash
set -e

# SnapTray macOS Packaging Script
# Creates a DMG with the application bundle
#
# Usage:
#   ./package.sh                    # Build without signing
#   CODESIGN_IDENTITY="..." ./package.sh  # Build with signing
#
# Optional environment variables:
#   CODESIGN_IDENTITY     - Developer ID for code signing
#   NOTARIZE_APPLE_ID     - Apple ID for notarization
#   NOTARIZE_TEAM_ID      - Team ID for notarization
#   NOTARIZE_PASSWORD     - App-specific password for notarization

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
PROJECT_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
BUILD_DIR="${PROJECT_ROOT}/release"
OUTPUT_DIR="${PROJECT_ROOT}/dist"

# Extract version from CMakeLists.txt
VERSION=$(grep "project(SnapTray VERSION" "$PROJECT_ROOT/CMakeLists.txt" | \
          sed -E 's/.*VERSION ([0-9]+\.[0-9]+\.[0-9]+).*/\1/')
APP_NAME="SnapTray"
DMG_NAME="${APP_NAME}-${VERSION}-macOS"

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

# Check for Qt
QT_PREFIX=$(brew --prefix qt 2>/dev/null || echo "")
if [ -z "$QT_PREFIX" ] || [ ! -d "$QT_PREFIX" ]; then
    echo -e "${RED}Error: Qt not found. Please install with: brew install qt${NC}"
    exit 1
fi
echo "Qt path: $QT_PREFIX"

# Step 1: Build Release
echo ""
echo -e "${YELLOW}[1/5] Building release...${NC}"
cmake -S "$PROJECT_ROOT" -B "$BUILD_DIR" \
    -DCMAKE_BUILD_TYPE=Release \
    -DCMAKE_PREFIX_PATH="$QT_PREFIX"
cmake --build "$BUILD_DIR" --config Release --parallel

# Check if app was built
if [ ! -d "$BUILD_DIR/${APP_NAME}.app" ]; then
    echo -e "${RED}Error: Build failed - ${APP_NAME}.app not found${NC}"
    exit 1
fi

# Step 2: Run macdeployqt
echo ""
echo -e "${YELLOW}[2/5] Running macdeployqt...${NC}"
"$QT_PREFIX/bin/macdeployqt" "$BUILD_DIR/${APP_NAME}.app" -verbose=1

# Step 3: Code signing
echo ""
echo -e "${YELLOW}[3/5] Signing application...${NC}"
if [ -n "$CODESIGN_IDENTITY" ]; then
    echo "Using Developer ID: $CODESIGN_IDENTITY"
    codesign --force --deep --sign "$CODESIGN_IDENTITY" \
        --options runtime \
        --entitlements "$SCRIPT_DIR/entitlements.plist" \
        "$BUILD_DIR/${APP_NAME}.app"
else
    echo "Using ad-hoc signing (no Developer ID configured)"
    codesign --force --deep --sign - \
        --options runtime \
        --entitlements "$SCRIPT_DIR/entitlements.plist" \
        "$BUILD_DIR/${APP_NAME}.app"
fi

echo "Verifying signature..."
codesign --verify --verbose "$BUILD_DIR/${APP_NAME}.app"
echo -e "${GREEN}Signature verified${NC}"

# Remove quarantine attribute to prevent Gatekeeper issues
echo "Removing quarantine attributes..."
xattr -cr "$BUILD_DIR/${APP_NAME}.app"

# Step 4: Create DMG
echo ""
echo -e "${YELLOW}[4/5] Creating DMG...${NC}"
mkdir -p "$OUTPUT_DIR"

# Remove existing DMG if present
rm -f "$OUTPUT_DIR/${DMG_NAME}.dmg"

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
        "$OUTPUT_DIR/${DMG_NAME}.dmg" \
        "$BUILD_DIR/${APP_NAME}.app" 2>/dev/null || {
            # Fallback if create-dmg fails
            echo "create-dmg failed, falling back to hdiutil..."
            hdiutil create -volname "$APP_NAME" \
                -srcfolder "$BUILD_DIR/${APP_NAME}.app" \
                -ov -format UDZO \
                "$OUTPUT_DIR/${DMG_NAME}.dmg"
        }
else
    # Fallback to hdiutil
    echo "Using hdiutil (install create-dmg for prettier output: brew install create-dmg)"
    hdiutil create -volname "$APP_NAME" \
        -srcfolder "$BUILD_DIR/${APP_NAME}.app" \
        -ov -format UDZO \
        "$OUTPUT_DIR/${DMG_NAME}.dmg"
fi

# Step 5: Notarization (if credentials provided)
echo ""
if [ -n "$NOTARIZE_APPLE_ID" ] && [ -n "$NOTARIZE_TEAM_ID" ] && [ -n "$NOTARIZE_PASSWORD" ]; then
    echo -e "${YELLOW}[5/5] Submitting for notarization...${NC}"
    xcrun notarytool submit "$OUTPUT_DIR/${DMG_NAME}.dmg" \
        --apple-id "$NOTARIZE_APPLE_ID" \
        --team-id "$NOTARIZE_TEAM_ID" \
        --password "$NOTARIZE_PASSWORD" \
        --wait

    echo "Stapling notarization ticket..."
    xcrun stapler staple "$OUTPUT_DIR/${DMG_NAME}.dmg"
    echo -e "${GREEN}Notarization complete${NC}"
else
    echo -e "${YELLOW}[5/5] Skipping notarization (credentials not set)${NC}"
fi

# Done
echo ""
echo -e "${GREEN}=== Build Complete ===${NC}"
echo -e "Package: ${GREEN}$OUTPUT_DIR/${DMG_NAME}.dmg${NC}"
echo ""
echo "To install:"
echo "  1. Open the DMG"
echo "  2. Drag SnapTray to Applications"
echo ""
if [ -z "$CODESIGN_IDENTITY" ]; then
    echo -e "${YELLOW}Note: This build uses ad-hoc signing (no Apple Developer ID).${NC}"
    echo "If macOS blocks the app, users can run:"
    echo -e "  ${GREEN}xattr -cr /Applications/SnapTray.app${NC}"
    echo ""
    echo "Or: Right-click the app → Open → Click 'Open' in the dialog"
fi
