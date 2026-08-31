#!/bin/bash
set -euo pipefail

APP_PATH="${1:-}"
PLIST_BUDDY="/usr/libexec/PlistBuddy"

fail() {
    echo "Error: $1" >&2
    exit 1
}

if [ -z "$APP_PATH" ]; then
    fail "Usage: $0 /path/to/SnapTray.app"
fi

if [ ! -d "$APP_PATH" ]; then
    fail "Application bundle not found: $APP_PATH"
fi

if [ ! -x "$PLIST_BUDDY" ]; then
    fail "PlistBuddy not found: $PLIST_BUDDY"
fi

INFO_PLIST="$APP_PATH/Contents/Info.plist"
if [ ! -f "$INFO_PLIST" ]; then
    fail "Packaged Info.plist not found: $INFO_PLIST"
fi

if ! plutil -lint "$INFO_PLIST" >/dev/null; then
    fail "Packaged Info.plist is invalid: $INFO_PLIST"
fi

MICROPHONE_DESCRIPTION="$("$PLIST_BUDDY" \
    -c 'Print :NSMicrophoneUsageDescription' \
    "$INFO_PLIST" 2>/dev/null || true)"
if [ -z "$MICROPHONE_DESCRIPTION" ]; then
    fail "Packaged Info.plist is missing a non-empty NSMicrophoneUsageDescription"
fi

ENTITLEMENTS_DUMP="$(mktemp "${TMPDIR:-/tmp}/snaptray-entitlements.XXXXXX")"
trap 'rm -f "$ENTITLEMENTS_DUMP"' EXIT

if ! codesign -d --entitlements - --xml "$APP_PATH" >"$ENTITLEMENTS_DUMP"; then
    fail "Unable to read entitlements from the signed application bundle"
fi

if ! plutil -lint "$ENTITLEMENTS_DUMP" >/dev/null; then
    fail "The signed application contains invalid entitlements"
fi

AUDIO_INPUT_ENTITLEMENT="$("$PLIST_BUDDY" \
    -c 'Print :com.apple.security.device.audio-input' \
    "$ENTITLEMENTS_DUMP" 2>/dev/null || true)"
if [ "$AUDIO_INPUT_ENTITLEMENT" != "true" ]; then
    fail "Signed application is missing com.apple.security.device.audio-input=true"
fi

echo "Packaged microphone usage description and audio-input entitlement verified."
