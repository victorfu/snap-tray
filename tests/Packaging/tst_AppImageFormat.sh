#!/usr/bin/env bash
set -euo pipefail

PROJECT_DIR="${1:-$(cd "$(dirname "$0")/../.." && pwd)}"

# shellcheck source=/dev/null
source "$PROJECT_DIR/packaging/linux/appimage-helpers.sh"

TMP_DIR="$(mktemp -d)"
trap 'rm -rf "$TMP_DIR"' EXIT

# Only the header is needed to exercise format detection. Use a path with spaces
# and verify that validation never changes the original bytes.
APPIMAGE="$TMP_DIR/test image.AppImage"
head -c 64 /dev/zero >"$APPIMAGE"
printf '\177ELF' | dd of="$APPIMAGE" bs=1 seek=0 conv=notrunc status=none
printf 'AI\002' | dd of="$APPIMAGE" bs=1 seek=8 conv=notrunc status=none
cp "$APPIMAGE" "$TMP_DIR/original"
chmod a-w "$APPIMAGE"
validate_appimage_header "$APPIMAGE"
cmp "$APPIMAGE" "$TMP_DIR/original"
chmod u+w "$APPIMAGE"

expect_invalid_header() {
  local appimage="$1"
  if validate_appimage_header "$appimage" >"$TMP_DIR/output" 2>&1; then
    echo "expected invalid AppImage header to be rejected: $appimage" >&2
    exit 1
  fi
}

# Reproduce the old AppImageLauncher workaround that broke catalog detection.
printf '\0\0\0' | dd of="$APPIMAGE" bs=1 seek=8 conv=notrunc status=none
cp "$APPIMAGE" "$TMP_DIR/masked"
expect_invalid_header "$APPIMAGE"
grep -q 'Invalid type 2 AppImage header' "$TMP_DIR/output"
cmp "$APPIMAGE" "$TMP_DIR/masked"

# A type 1 marker, ordinary ELF, non-ELF data, and truncated/missing files must
# not pass merely because they have an .AppImage extension.
printf 'AI\001' | dd of="$APPIMAGE" bs=1 seek=8 conv=notrunc status=none
expect_invalid_header "$APPIMAGE"
cp "$TMP_DIR/original" "$APPIMAGE"
printf 'nope' | dd of="$APPIMAGE" bs=1 seek=0 conv=notrunc status=none
expect_invalid_header "$APPIMAGE"
head -c 10 "$TMP_DIR/original" >"$APPIMAGE"
expect_invalid_header "$APPIMAGE"
: >"$APPIMAGE"
expect_invalid_header "$APPIMAGE"
expect_invalid_header "$TMP_DIR/missing.AppImage"
