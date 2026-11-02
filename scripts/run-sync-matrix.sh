#!/usr/bin/env bash
# Runs the recording sync matrix (#163) and keeps a JSON report per scenario.
#   scripts/run-sync-matrix.sh [REPORT_DIR] [--full]
# --full adds the 4K and long rows (SNAPTRAY_SYNC_LONG_S sets the long length).
# Compare two runs with scripts/sync-matrix-compare.py BASELINE CANDIDATE.
# REPORT_DIR holds this run's reports only: earlier reports in it are removed
# first, so a row this run skips can never pass as rerun (#178 review).
set -euo pipefail
cd "$(dirname "$0")/.."
REPORT_DIR=""
FULL=0
for arg in "$@"; do
    case "$arg" in
        --full) FULL=1 ;;
        -*) echo "unknown option: $arg" >&2; exit 2 ;;
        *)
            if [ -n "$REPORT_DIR" ]; then
                echo "only one REPORT_DIR may be given" >&2
                exit 2
            fi
            REPORT_DIR="$arg"
            ;;
    esac
done
REPORT_DIR="${REPORT_DIR:-build/sync-matrix/$(git rev-parse --short HEAD 2>/dev/null || echo local)}"
cmake --build build --target Encoding_SyncMatrix
mkdir -p "$REPORT_DIR"
stale=$(find "$REPORT_DIR" -maxdepth 1 -type f -name '*.json' | wc -l)
if [ "$stale" -gt 0 ]; then
    echo "Removing $stale report(s) from an earlier run in $REPORT_DIR"
    find "$REPORT_DIR" -maxdepth 1 -type f -name '*.json' -delete
fi
SNAPTRAY_SYNC_FULL="$FULL" SNAPTRAY_SYNC_REPORT_DIR="$REPORT_DIR" QT_QPA_PLATFORM=offscreen \
    ./build/bin/Encoding_SyncMatrix
echo "Reports: $REPORT_DIR"
