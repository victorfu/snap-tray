#!/usr/bin/env bash
# Runs the recording sync matrix (#163) and keeps a JSON report per scenario.
#   scripts/run-sync-matrix.sh [REPORT_DIR] [--full]
# --full adds the 4K and long rows (SNAPTRAY_SYNC_LONG_S sets the long length).
# Compare two runs with scripts/sync-matrix-compare.py BASELINE CANDIDATE.
set -euo pipefail
cd "$(dirname "$0")/.."
REPORT_DIR="${1:-build/sync-matrix/$(git rev-parse --short HEAD 2>/dev/null || echo local)}"
FULL=0
for arg in "$@"; do [ "$arg" = "--full" ] && FULL=1; done
cmake --build build --target Encoding_SyncMatrix
mkdir -p "$REPORT_DIR"
SNAPTRAY_SYNC_FULL="$FULL" SNAPTRAY_SYNC_REPORT_DIR="$REPORT_DIR" QT_QPA_PLATFORM=offscreen \
    ./build/bin/Encoding_SyncMatrix
echo "Reports: $REPORT_DIR"
