#!/bin/bash
# Enforce token-only colors/timing literals for RecordingPreview QML.

set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(dirname "$SCRIPT_DIR")"
TARGET="$PROJECT_DIR/src/qml/recording/RecordingPreview.qml"

if [ ! -f "$TARGET" ]; then
    echo "ERROR: Target file not found: $TARGET"
    exit 1
fi

if command -v rg >/dev/null 2>&1; then
    MATCH_CMD=(rg -n "#[0-9A-Fa-f]{3,8}|Qt\\.rgba\\(" "$TARGET")
else
    MATCH_CMD=(grep -nE "#[0-9A-Fa-f]{3,8}|Qt\.rgba\(" "$TARGET")
fi

if "${MATCH_CMD[@]}" >/tmp/recording-preview-token-check.out 2>/dev/null; then
    echo "ERROR: Found hardcoded color literals in RecordingPreview.qml"
    cat /tmp/recording-preview-token-check.out
    rm -f /tmp/recording-preview-token-check.out
    exit 1
fi
rm -f /tmp/recording-preview-token-check.out

echo "OK: RecordingPreview.qml uses token-based colors only"
