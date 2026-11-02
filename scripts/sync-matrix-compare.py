#!/usr/bin/env python3
"""Compare two recording sync-matrix report directories (A/B across commits).

Usage: scripts/sync-matrix-compare.py BASELINE_DIR CANDIDATE_DIR

Each directory holds one JSON report per scenario, written by
Encoding_SyncMatrix with SNAPTRAY_SYNC_REPORT_DIR set. Exit status 1 when a
correctness result got worse (a passing row fails, or the A/V offset grows by
more than OFFSET_REGRESSION_MS); cost changes are reported, not judged.
"""
import json
import sys
from pathlib import Path

OFFSET_REGRESSION_MS = 10
COST_KEYS = ("wallMs", "cpuMs", "rssKiB", "fileBytes")
QUALITY_KEYS = ("worstAvOffsetMs", "uniqueFps", "missing", "unexpected", "misplaced", "outOfOrder")


def load(directory):
    reports = {}
    for path in sorted(Path(directory).glob("*.json")):
        with open(path, encoding="utf-8") as handle:
            report = json.load(handle)
        reports[report["scenario"]["name"]] = report
    return reports


def delta(before, after):
    if before is None or after is None:
        return "n/a"
    if isinstance(before, (int, float)) and before:
        return f"{after - before:+.4g} ({(after - before) / before * 100:+.1f}%)"
    return f"{before} -> {after}"


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    baseline, candidate = load(sys.argv[1]), load(sys.argv[2])
    regressions = []
    for name in sorted(set(baseline) | set(candidate)):
        a, b = baseline.get(name), candidate.get(name)
        if a is None or b is None:
            print(f"{name}: only in {'candidate' if a is None else 'baseline'}")
            continue
        status = f"{'pass' if a['passed'] else 'FAIL'} -> {'pass' if b['passed'] else 'FAIL'}"
        print(f"{name}: {status}")
        if a["passed"] and not b["passed"]:
            regressions.append(f"{name}: now fails: {'; '.join(b['failures'])}")
        for key in QUALITY_KEYS:
            va, vb = a["measured"].get(key), b["measured"].get(key)
            if va != vb:
                print(f"  {key}: {delta(va, vb)}")
        offset_a = a["measured"].get("worstAvOffsetMs")
        offset_b = b["measured"].get("worstAvOffsetMs")
        if offset_a is not None and offset_b is not None and offset_b - offset_a > OFFSET_REGRESSION_MS:
            regressions.append(f"{name}: A/V offset {offset_a} -> {offset_b} ms")
        for key in COST_KEYS:
            print(f"  cost {key}: {delta(a['cost'].get(key), b['cost'].get(key))}")
        for skipped in b.get("skipped", []):
            print(f"  skipped: {skipped}")
    if regressions:
        print("\nRegressions:")
        for line in regressions:
            print(f"  {line}")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
