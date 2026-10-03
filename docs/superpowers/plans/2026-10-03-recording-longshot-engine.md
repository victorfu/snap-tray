# Long Screenshot Engine (Phase 4a) Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the offline long-screenshot engine — sequential frame decoding on both platforms, a global position solver, row-feature analysis with static-band masks, a tile renderer with seams and a height cap, a cache contract — plus the synthetic evaluation harness that proves it against ground truth, with no UI yet.

**Architecture:** Everything lives in `snaptray_algorithms` under `src/longshot/` with OpenCV-free public headers in `include/longshot/` (QImage/QRect/std types only; OpenCV is used privately for phase correlation and normalized cross-correlation). Frames come through the existing `IVideoFrameReader` interface, which gains a Windows Media Foundation implementation; a thin adapter turns it into the trim-and-crop-aware `LongshotFrameSource`. The pipeline is two passes: `LongshotAnalyzer` extracts per-frame features and per-pair shifts with masks, `PositionSolver` turns pair shifts into global positions (weighted least squares, loop closures, outlier rejection, islands reported as breaks), and `LongshotRenderer` picks one source frame per 64-row tile and writes the tall image. `LongshotSession` owns the cache contract. `tests/Longshot/` holds a synthetic harness that renders a tall ground-truth page, simulates scroll trajectories and disturbances, encodes with the real native encoder, runs the engine, and scores duplicated/missing/misaligned rows.

**Tech Stack:** Qt 6.11 (C++17), OpenCV 4.10 core/imgproc (already fetched; private to `snaptray_algorithms`), Media Foundation (`IMFSourceReader`) on Windows, AVFoundation (`AVFoundationFrameReader`, existing) on macOS, Qt Test; CMake + Ninja.

**Spec:** `docs/superpowers/plans/2026-10-02-region-recording-longshot-roadmap.md`, sections "Design rationale", "Global Constraints", "Order and dependencies" and "Phase 4" (tasks 1–5 and 7 of its task list; tasks 6, 8, 9 — corpus/eval CLI, preview UX, translations — are Phase 4b, planned after this engine merges, as the roadmap's "details based on the code as it actually stands" rule requires).

## Global Constraints

- Recording always captures the full screen. Region choice happens after recording, in RecordingPreview. Do not touch Region Selector. `showPreview = false` behaviour stays unchanged.
- macOS 14+ and Windows 10+. Linux beta keeps recording hidden; new code compiles there but stays inactive (`IVideoFrameReader::create()` returns nullptr there; every engine entry point must fail cleanly on a null source).
- All rects that cross from recording to Preview are in **video pixels**. Preview never handles DPI.
- Sidecar data never records window titles, and it never reaches final outputs or History.
- Write the evaluation harness before UI (phase 4). When stitching cannot be done confidently, report it. Never guess a join.
- Phase 4 scope: vertical only, one main scroll region, user scrolls by hand, recordings contain no cursor.
- Coordinate and mask contract (verbatim): recompute fixed-region observations per frame pair, then retain the resolved per-frame bands and valid content rectangles in Pass 1 results; a header that appears only while scrolling up must not become a global top crop. Removing a header for matching does not reset the coordinate origin; pair shifts and solved positions refer to the original crop-local frame origin. Rendering samples only valid pixels from the selected frame; if a tile has no reliable coverage, report a break instead of filling it by guesswork. Include a sticky header once at the top only when requested and a valid source observation exists.
- Cache contract (verbatim): a bounded decode cache is separate from crop-dependent analysis; reuse decoded frames only when source identity and timestamps match; do not retain all full-resolution frames in RAM. Analysis cache keys include source identity, normalized crop, and analysis version/options; changing crop invalidates features, masks, shifts, positions and rendered tiles. A trim change may reuse unchanged per-frame base features within the overlap and must rerun solving and rendering. Rendering-only options reuse compatible analysis but invalidate output tiles. Test every invalidation path. Use a named memory budget.
- Confidence is the best score plus its margin over the runner-up; an ambiguous match is rejected.
- Renderer: one source frame per 64-row tile, scoring stationary frames, keyframes and tile-mid-frame position higher, rejecting frames that disagree with the majority; seams on low-gradient rows; later observations win on conflicts; sticky header off by default; static side columns auto-cropped; PNG output with the ~30000 px cap and an option to split.
- Harness encodes with the real native encoder at intermediate quality (`setRateControl(ConstantQuality, IntermediateQuality::kConstantQualityValue)`, 1 s keyframes).
- AGENTS.md: named constants instead of magic numbers, `qWarning()` on API failures, `qDebug()` for diagnostics, `QPoint`/`QRect` over loose coordinates, tests under `tests/<Component>/` registered in `tests/CMakeLists.txt`.

## Review Focus

1. A recording where the user never scrolled (every frame stationary) — the result must be a single-frame-tall image with no breaks and no duplicated rows, not an empty image or an error. → Task 5 `stationaryRecordingYieldsOneFrame`, Task 6 `renderStationary`.
2. A page with a large uniform area (blank space taller than the viewport) — shifts across it are ambiguous and must be rejected, producing a reported break rather than an invented position. → Task 4 `uniformRegionIsAmbiguous`, Task 5 `blankGapReportsBreak`.
3. A trim range shorter than two frames, or a crop smaller than the minimum analysable size — the engine must return a clear failure (`LongshotError::TooFewFrames` / `CropTooSmall`), never crash or loop. → Task 5 `tooFewFramesFails`, Task 3 `openRejectsTinyCrop`.
4. A sticky header that covers more than half the viewport (e.g. a tall toolbar) — the header mask must not swallow the scrolling content; if too little content remains, the pair must be rejected rather than matched on header pixels. → Task 4 `tallHeaderLeavesContentOrRejects`.
5. Output taller than the 30000 px cap — the renderer must either split into parts (when asked) or report `heightCapped` with the truncated height, never allocate an oversized `QImage` that fails silently. → Task 6 `heightCapSplitsOrReports`.

---

## File Structure

| File | Responsibility |
| --- | --- |
| `include/longshot/LongshotTypes.h` (create) | Plain data: `StaticBands`, `FrameFeatures`, `PairShift`, `SolveResult`, `LongshotOptions`, `LongshotError`, constants. |
| `include/longshot/PositionSolver.h`, `src/longshot/PositionSolver.cpp` (create) | Pure solver: weighted least squares on pair shifts, loop closures, outlier rejection, islands → breaks. |
| `include/longshot/LongshotFrameSource.h` (create) | Interface: `open(path, startMs, endMs, crop)`, `next(tMs)`. |
| `include/video/IVideoFrameReader.h` (modify, comment only), `src/video/MediaFoundationFrameReader_win.cpp` (create), `src/video/IVideoFrameReader.cpp` (modify) | Windows sequential decoder (spec task 7). |
| `include/longshot/FrameReaderLongshotSource.h`, `src/longshot/FrameReaderLongshotSource.cpp` (create) | Adapter from `IVideoFrameReader` to `LongshotFrameSource` (trim + crop + frame cadence). Lives in `snaptray_platform`. |
| `include/longshot/LongshotAnalyzer.h`, `src/longshot/LongshotAnalyzer.cpp` (create) | Pass 1: grayscale, row features, static bands per pair, 1-D phase correlation, NCC refinement, confidence. |
| `include/longshot/LongshotPipeline.h`, `src/longshot/LongshotPipeline.cpp` (create) | Pass 1 driver + edge building (chain + loop closure) + solve + island rejoin. |
| `include/longshot/LongshotRenderer.h`, `src/longshot/LongshotRenderer.cpp` (create) | Pass 2: tile selection, seams, side crop, sticky header, height cap/split. |
| `include/longshot/LongshotSession.h`, `src/longshot/LongshotSession.cpp` (create) | Cache contract: decode cache, analysis cache keys, invalidation. |
| `tests/Longshot/SyntheticScroll.h`, `tests/Longshot/SyntheticScroll.cpp` (create) | Harness library: ground-truth page, trajectories, disturbances, native-encoder recording, row scoring. |
| `tests/Longshot/tst_PositionSolver.cpp`, `tst_FrameSource.cpp`, `tst_SyntheticHarness.cpp`, `tst_LongshotAnalyzer.cpp`, `tst_LongshotPipeline.cpp`, `tst_LongshotRenderer.cpp`, `tst_LongshotSession.cpp` (create) | Tests. |
| `CMakeLists.txt`, `tests/CMakeLists.txt` (modify) | Sources into `snaptray_algorithms` / `snaptray_platform`; test targets. |

Build notes for every task (Windows host): worktree `D:\Documents\snap-tray\.claude\worktrees\recording-longshot`; configure once with `cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Debug -DCMAKE_PREFIX_PATH=C:/Qt/6.11.2/msvc2022_64 -DQt6_DIR=C:/Qt/6.11.2/msvc2022_64/lib/cmake/Qt6` through the vcvars helper; test executables are in `build\bin\` and must be run with `-o <file>,txt` because their stdout is not captured. OpenCV headers are only visible inside `snaptray_algorithms` and in tests that copy the include-dir block used by `Annotations_MosaicStroke` (`${CMAKE_BINARY_DIR}`, `${CMAKE_BINARY_DIR}/_deps/opencv-src/modules/core/include`, `.../imgproc/include`).

---

### Task 1: PositionSolver

**Files:**
- Create: `include/longshot/LongshotTypes.h`
- Create: `include/longshot/PositionSolver.h`, `src/longshot/PositionSolver.cpp`
- Create: `tests/Longshot/tst_PositionSolver.cpp`
- Modify: `CMakeLists.txt` (the `snaptray_algorithms` source list, ~line 441), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: nothing.
- Produces: `SnapTray::Longshot::{StaticBands, FrameFeatures, PairShift, SolveResult, LongshotOptions, LongshotError}` and `SnapTray::Longshot::PositionSolver::solve(const std::vector<qint64>& frameTimesMs, const std::vector<PairShift>& edges, double maxResidualPx) -> SolveResult`. Positions are `std::optional<int>` per frame (nullopt = not on the kept island), `breakTimesMs` lists the first frame time of every island other than the kept one. Later tasks use these names exactly.

- [ ] **Step 1: Write the failing test**

`tests/Longshot/tst_PositionSolver.cpp`:

```cpp
#include "longshot/PositionSolver.h"

#include <QtTest>

using namespace SnapTray::Longshot;

namespace {

std::vector<qint64> times(int count)
{
    std::vector<qint64> t(count);
    for (int i = 0; i < count; ++i) t[i] = qint64(i) * 100;
    return t;
}

PairShift edge(int from, int to, int dy, double confidence = 1.0)
{
    PairShift e;
    e.from = from;
    e.to = to;
    e.dy = dy;
    e.confidence = confidence;
    return e;
}

} // namespace

class tst_PositionSolver : public QObject
{
    Q_OBJECT
private slots:
    void chainIsCumulative();
    void loopClosureDistributesDrift();
    void outlierEdgeIsRejected();
    void disconnectedIslandsReportBreaks();
    void keepsLargestIsland();
    void roundsToIntegers();
    void emptyInputs();
    void zeroConfidenceEdgeIgnored();
};

void tst_PositionSolver::chainIsCumulative()
{
    const auto result = PositionSolver::solve(times(4), {edge(0, 1, 10), edge(1, 2, 20), edge(2, 3, -5)}, 3.0);
    QCOMPARE(result.positions.size(), size_t(4));
    QCOMPARE(*result.positions[0], 0);
    QCOMPARE(*result.positions[1], 10);
    QCOMPARE(*result.positions[2], 30);
    QCOMPARE(*result.positions[3], 25);
    QVERIFY(result.breakTimesMs.empty());
    QCOMPARE(result.rejectedEdges, 0);
}

void tst_PositionSolver::loopClosureDistributesDrift()
{
    // Chain says 0->3 is 30; the closure says 27. Least squares spreads the 3 px
    // disagreement across the four equally-weighted edges instead of putting it
    // all on one.
    const auto result = PositionSolver::solve(
        times(4), {edge(0, 1, 10), edge(1, 2, 10), edge(2, 3, 10), edge(0, 3, 27)}, 5.0);
    QCOMPARE(*result.positions[0], 0);
    QVERIFY(qAbs(*result.positions[3] - 28) <= 1); // 27.75 rounds to 28
    QVERIFY(*result.positions[1] >= 9 && *result.positions[1] <= 10);
    QVERIFY(result.breakTimesMs.empty());
    QCOMPARE(result.rejectedEdges, 0);
}

void tst_PositionSolver::outlierEdgeIsRejected()
{
    // One wildly wrong closure must be dropped, leaving the chain intact.
    const auto result = PositionSolver::solve(
        times(4), {edge(0, 1, 10), edge(1, 2, 10), edge(2, 3, 10), edge(0, 3, 300, 0.5)}, 4.0);
    QCOMPARE(*result.positions[3], 30);
    QCOMPARE(result.rejectedEdges, 1);
    QVERIFY(result.breakTimesMs.empty());
}

void tst_PositionSolver::disconnectedIslandsReportBreaks()
{
    // Frames 0-2 connected, 3-5 connected, nothing between 2 and 3.
    const auto result = PositionSolver::solve(
        times(6), {edge(0, 1, 10), edge(1, 2, 10), edge(3, 4, 10), edge(4, 5, 10)}, 3.0);
    QVERIFY(result.positions[0].has_value());
    QVERIFY(result.positions[2].has_value());
    QVERIFY(!result.positions[3].has_value());
    QVERIFY(!result.positions[5].has_value());
    QCOMPARE(result.breakTimesMs, (std::vector<qint64>{300}));
}

void tst_PositionSolver::keepsLargestIsland()
{
    // Island A = {0,1}, island B = {2,3,4}: B is larger, so B is kept and A is the break.
    const auto result = PositionSolver::solve(
        times(5), {edge(0, 1, 10), edge(2, 3, 10), edge(3, 4, 10)}, 3.0);
    QVERIFY(!result.positions[0].has_value());
    QVERIFY(result.positions[2].has_value());
    QCOMPARE(*result.positions[2], 0); // the kept island is anchored at its first frame
    QCOMPARE(*result.positions[4], 20);
    QCOMPARE(result.breakTimesMs, (std::vector<qint64>{0}));
}

void tst_PositionSolver::roundsToIntegers()
{
    // Two conflicting equally weighted edges 0->1: 10 and 11 -> 10.5 -> rounds half away from zero to 11.
    const auto result = PositionSolver::solve(times(2), {edge(0, 1, 10), edge(0, 1, 11)}, 3.0);
    QCOMPARE(*result.positions[1], 11);
}

void tst_PositionSolver::emptyInputs()
{
    const auto none = PositionSolver::solve({}, {}, 3.0);
    QVERIFY(none.positions.empty());
    QVERIFY(none.breakTimesMs.empty());
    // One frame, no edges: a single island at position 0.
    const auto single = PositionSolver::solve(times(1), {}, 3.0);
    QCOMPARE(single.positions.size(), size_t(1));
    QCOMPARE(*single.positions[0], 0);
    // Frames without edges each form their own island; the first is kept.
    const auto isolated = PositionSolver::solve(times(3), {}, 3.0);
    QVERIFY(isolated.positions[0].has_value());
    QVERIFY(!isolated.positions[1].has_value());
    QCOMPARE(isolated.breakTimesMs, (std::vector<qint64>{100, 200}));
}

void tst_PositionSolver::zeroConfidenceEdgeIgnored()
{
    const auto result = PositionSolver::solve(times(3), {edge(0, 1, 10), edge(1, 2, 10, 0.0)}, 3.0);
    QVERIFY(!result.positions[2].has_value());
    QCOMPARE(result.breakTimesMs, (std::vector<qint64>{200}));
}

QTEST_APPLESS_MAIN(tst_PositionSolver)
#include "tst_PositionSolver.moc"
```

Append to `tests/CMakeLists.txt` (new section after the Detection tests):

```cmake
# ============================================================================
# Longshot Tests (link snaptray_algorithms)
# ============================================================================

add_executable(Longshot_PositionSolver Longshot/tst_PositionSolver.cpp)
target_link_libraries(Longshot_PositionSolver PRIVATE snaptray_algorithms Qt6::Test)
add_test(NAME Longshot_PositionSolver COMMAND Longshot_PositionSolver)
set_tests_properties(Longshot_PositionSolver PROPERTIES TIMEOUT 60 LABELS "unit")
```

- [ ] **Step 2: Run the test to verify it fails**

Build `Longshot_PositionSolver`. Expected: `Cannot open include file: 'longshot/PositionSolver.h'`.

- [ ] **Step 3: Write the types header**

`include/longshot/LongshotTypes.h`:

```cpp
#pragma once

#include <QRect>
#include <QSize>
#include <QtGlobal>

#include <optional>
#include <vector>

// Shared plain data for the long-screenshot engine. Everything here is in
// crop-local video pixels: (0, 0) is the top-left of the normalized crop of
// the recorded frame. Nothing in this header depends on OpenCV.
namespace SnapTray::Longshot {

// Renderer tile height in output rows.
constexpr int kTileRows = 64;
// Default output height cap (PNG stays practical for viewers and clipboards).
constexpr int kDefaultMaxHeightPx = 30000;
// Analysis frames narrower or shorter than this cannot be matched reliably.
constexpr int kMinAnalysisSide = 64;
// Version of the analysis algorithm; part of every analysis cache key.
constexpr int kAnalysisVersion = 1;

// Crop-local pixel bands excluded from matching and rendering for ONE frame
// (sticky header, bottom bar, side panels). Zero means nothing excluded.
struct StaticBands {
    int top = 0;
    int bottom = 0;
    int left = 0;
    int right = 0;
    bool operator==(const StaticBands& o) const
    {
        return top == o.top && bottom == o.bottom && left == o.left && right == o.right;
    }
};

struct FrameFeatures {
    qint64 tMs = 0;
    std::vector<float> rowMean;      // mean luma per crop-local row
    std::vector<float> rowGradient;  // mean |vertical luma gradient| per row
    bool stationary = false;         // no motion relative to the previous frame
    bool keyFrame = false;           // encoder keyframe (from the reader when known)
    StaticBands excludedBands;       // resolved for this frame from its pair observations
    QRect validContentRect;          // crop-local rect that rendering may sample from
};

// Observation: pageOffset(to) - pageOffset(from) == dy. Positive dy means the
// user scrolled down between the two frames (content moved up on screen).
// Confidence is best score + margin over the runner-up, in [0, 1]; 0 = unusable.
struct PairShift {
    int from = 0;
    int to = 0;
    int dy = 0;
    double confidence = 0.0;
};

struct SolveResult {
    std::vector<std::optional<int>> positions; // page offset of each frame's crop origin; nullopt = not placed
    std::vector<qint64> breakTimesMs;          // first frame time of every island that was not kept
    int rejectedEdges = 0;                     // outliers dropped during solving
};

struct LongshotOptions {
    bool includeStickyHeader = false;
    int maxHeightPx = kDefaultMaxHeightPx;
    int tileRows = kTileRows;
    bool splitOversize = false; // true: return several parts instead of capping
    bool operator==(const LongshotOptions& o) const
    {
        return includeStickyHeader == o.includeStickyHeader && maxHeightPx == o.maxHeightPx
               && tileRows == o.tileRows && splitOversize == o.splitOversize;
    }
};

enum class LongshotError {
    None,
    SourceUnavailable, // no frame reader on this platform or file failed to open
    TooFewFrames,      // fewer than two decodable frames in the trim range
    CropTooSmall,      // crop below kMinAnalysisSide
    Cancelled,
    NoReliableContent, // every pair was rejected; nothing to place
};

} // namespace SnapTray::Longshot
```

- [ ] **Step 4: Write the solver**

`include/longshot/PositionSolver.h`:

```cpp
#pragma once

#include "longshot/LongshotTypes.h"

namespace SnapTray::Longshot {

// Turns pairwise shift observations into one page position per frame.
//
// Model: position[to] - position[from] = dy, weighted by confidence. Solved
// by weighted least squares (Gauss-Seidel on the normal equations, which is
// exact for this Laplacian system and needs no external library). Edges whose
// residual exceeds maxResidualPx are dropped one at a time (worst first) and
// the system re-solved, so a wrong loop closure cannot bend a good chain.
// Frames not connected to the largest island get no position; the first frame
// time of each dropped island is reported as a break. The kept island is
// anchored so its first frame sits at position 0. Positions are rounded to
// the nearest integer row.
class PositionSolver
{
public:
    static SolveResult solve(const std::vector<qint64>& frameTimesMs,
                             const std::vector<PairShift>& edges,
                             double maxResidualPx);
};

} // namespace SnapTray::Longshot
```

`src/longshot/PositionSolver.cpp`:

```cpp
#include "longshot/PositionSolver.h"

#include <QDebug>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace SnapTray::Longshot {

namespace {

// Gauss-Seidel converges geometrically on this diagonally dominant system;
// 2000 sweeps at 1e-4 is far beyond what sub-pixel rounding can observe.
constexpr int kMaxSweeps = 2000;
constexpr double kConvergenceTolerancePx = 1e-4;

struct Edge {
    int from;
    int to;
    double dy;
    double weight;
    bool active;
};

// Union-find over frames connected by active edges.
int findRoot(std::vector<int>& parent, int i)
{
    while (parent[i] != i) {
        parent[i] = parent[parent[i]];
        i = parent[i];
    }
    return i;
}

std::vector<int> components(int frameCount, const std::vector<Edge>& edges)
{
    std::vector<int> parent(frameCount);
    std::iota(parent.begin(), parent.end(), 0);
    for (const Edge& e : edges) {
        if (!e.active) continue;
        const int a = findRoot(parent, e.from);
        const int b = findRoot(parent, e.to);
        if (a != b) parent[a] = b;
    }
    std::vector<int> root(frameCount);
    for (int i = 0; i < frameCount; ++i) root[i] = findRoot(parent, i);
    return root;
}

// Weighted least squares for one island, anchored at `anchor` = 0.
std::vector<double> solveIsland(int frameCount, const std::vector<Edge>& edges,
                                const std::vector<int>& root, int islandRoot, int anchor)
{
    std::vector<double> pos(frameCount, 0.0);
    std::vector<double> diag(frameCount, 0.0);
    std::vector<std::vector<std::pair<int, double>>> neighbours(frameCount); // (other, weight)
    std::vector<double> rhs(frameCount, 0.0);
    for (const Edge& e : edges) {
        if (!e.active || root[e.from] != islandRoot) continue;
        diag[e.from] += e.weight;
        diag[e.to] += e.weight;
        neighbours[e.from].push_back({e.to, e.weight});
        neighbours[e.to].push_back({e.from, e.weight});
        rhs[e.from] -= e.weight * e.dy; // pos[from] = pos[to] - dy
        rhs[e.to] += e.weight * e.dy;   // pos[to] = pos[from] + dy
    }
    for (int sweep = 0; sweep < kMaxSweeps; ++sweep) {
        double maxDelta = 0.0;
        for (int i = 0; i < frameCount; ++i) {
            if (root[i] != islandRoot || i == anchor || diag[i] <= 0.0) continue;
            double sum = rhs[i];
            for (const auto& [other, weight] : neighbours[i]) sum += weight * pos[other];
            const double updated = sum / diag[i];
            maxDelta = std::max(maxDelta, std::abs(updated - pos[i]));
            pos[i] = updated;
        }
        if (maxDelta < kConvergenceTolerancePx) break;
    }
    return pos;
}

} // namespace

SolveResult PositionSolver::solve(const std::vector<qint64>& frameTimesMs,
                                  const std::vector<PairShift>& observations,
                                  double maxResidualPx)
{
    SolveResult result;
    const int frameCount = int(frameTimesMs.size());
    result.positions.assign(frameCount, std::nullopt);
    if (frameCount == 0) return result;

    std::vector<Edge> edges;
    edges.reserve(observations.size());
    for (const PairShift& o : observations) {
        if (o.confidence <= 0.0 || o.from < 0 || o.to < 0 || o.from >= frameCount || o.to >= frameCount
            || o.from == o.to) {
            continue;
        }
        edges.push_back({o.from, o.to, double(o.dy), o.confidence, true});
    }

    // Outlier rejection: solve, drop the single worst edge above the residual
    // cap, repeat. Dropping one at a time keeps a good chain from being
    // punished for a bad closure that it shares nodes with.
    std::vector<int> root;
    std::vector<double> pos;
    for (;;) {
        root = components(frameCount, edges);
        pos.assign(frameCount, 0.0);
        std::vector<bool> solved(frameCount, false);
        for (int i = 0; i < frameCount; ++i) {
            if (solved[i]) continue;
            const int islandRoot = root[i];
            const std::vector<double> islandPos = solveIsland(frameCount, edges, root, islandRoot, i);
            for (int j = 0; j < frameCount; ++j) {
                if (root[j] == islandRoot) {
                    pos[j] = islandPos[j];
                    solved[j] = true;
                }
            }
        }
        int worst = -1;
        double worstResidual = maxResidualPx;
        for (int k = 0; k < int(edges.size()); ++k) {
            const Edge& e = edges[k];
            if (!e.active) continue;
            const double residual = std::abs(pos[e.to] - pos[e.from] - e.dy);
            if (residual > worstResidual) {
                worstResidual = residual;
                worst = k;
            }
        }
        if (worst < 0) break;
        edges[worst].active = false;
        ++result.rejectedEdges;
        qDebug() << "PositionSolver: rejected edge" << edges[worst].from << "->" << edges[worst].to
                 << "dy" << edges[worst].dy << "residual" << worstResidual;
    }

    // Keep the largest island (ties: the one containing the earliest frame).
    std::vector<int> islandSize(frameCount, 0);
    for (int i = 0; i < frameCount; ++i) ++islandSize[root[i]];
    int keptRoot = root[0];
    for (int i = 0; i < frameCount; ++i) {
        if (islandSize[root[i]] > islandSize[keptRoot]) keptRoot = root[i];
    }
    int anchor = -1;
    for (int i = 0; i < frameCount; ++i) {
        if (root[i] == keptRoot) {
            anchor = i;
            break;
        }
    }
    const double offset = pos[anchor];
    std::vector<int> seenRoots;
    for (int i = 0; i < frameCount; ++i) {
        if (root[i] == keptRoot) {
            result.positions[i] = int(std::lround(pos[i] - offset));
        } else if (std::find(seenRoots.begin(), seenRoots.end(), root[i]) == seenRoots.end()) {
            seenRoots.push_back(root[i]);
            result.breakTimesMs.push_back(frameTimesMs[i]);
        }
    }
    std::sort(result.breakTimesMs.begin(), result.breakTimesMs.end());
    return result;
}

} // namespace SnapTray::Longshot
```

Add to the `snaptray_algorithms` source list in `CMakeLists.txt` (after `src/detection/TableDetector.cpp`):

```cmake
    src/longshot/PositionSolver.cpp
```

- [ ] **Step 5: Run the test to verify it passes**

Build and run `Longshot_PositionSolver` (`-o <scratch>/t1.txt,txt`). Expected: 8 slots pass. If `loopClosureDistributesDrift` lands on 27 instead of 28, check the sign convention in `rhs` (pos[to] − pos[from] = dy) before touching the test.

- [ ] **Step 6: Commit**

```bash
git add include/longshot/LongshotTypes.h include/longshot/PositionSolver.h src/longshot/PositionSolver.cpp tests/Longshot/tst_PositionSolver.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(longshot): add the global position solver"
```

---
### Task 2: Windows sequential frame reader and the longshot frame source

**Files:**
- Create: `src/video/MediaFoundationFrameReader_win.cpp`
- Modify: `src/video/IVideoFrameReader.cpp` (factory), `include/video/IVideoFrameReader.h` (comment only)
- Create: `include/longshot/LongshotFrameSource.h`
- Create: `include/longshot/FrameReaderLongshotSource.h`, `src/longshot/FrameReaderLongshotSource.cpp`
- Create: `tests/Longshot/tst_FrameSource.cpp`
- Modify: `CMakeLists.txt` (`snaptray_platform` sources, Windows-only list ~line 560 and common list), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `IVideoFrameReader` (`load`, `frameAt(positionMs)` ascending-only, `videoSize`, `duration`, `frameRate`, `lastError`, `create()`), `MediaFoundationFrameCopy_win.h::copyMediaFoundationRgb32Frame(const uchar*, qsizetype pitch, const QSize&)`, `VideoCropGeometry::normalizeCropRect`.
- Produces: `SnapTray::Longshot::LongshotFrameSource` (`virtual bool open(const QString& path, qint64 startMs, qint64 endMs, const QRect& crop) = 0; virtual std::optional<QImage> next(qint64* tMs) = 0; virtual QSize frameSize() const = 0; virtual QSize videoSize() const = 0; virtual double frameRate() const = 0; virtual int expectedFrameCount() const = 0; virtual QString lastError() const = 0;`) and `SnapTray::Longshot::FrameReaderLongshotSource` (`explicit FrameReaderLongshotSource(std::unique_ptr<IVideoFrameReader> reader)`; `static std::unique_ptr<LongshotFrameSource> createNative()` returning nullptr when the platform has no reader). `IVideoFrameReader::create()` now returns a Media Foundation reader on Windows. Frames from `next()` are `QImage::Format_RGB32`, already cropped to the normalized crop, sized `frameSize()`; `*tMs` is the frame's media time. Tasks 3–7 consume these.

- [ ] **Step 1: Write the failing tests**

`tests/Longshot/tst_FrameSource.cpp`:

```cpp
#include "IVideoEncoder.h"
#include "encoding/IntermediateQuality.h"
#include "encoding/VideoRateControl.h"
#include "longshot/FrameReaderLongshotSource.h"
#include "video/IVideoFrameReader.h"

#include <QtTest>
#include <QImage>
#include <QPainter>
#include <QTemporaryDir>

using namespace SnapTray;
using namespace SnapTray::Longshot;

namespace {

constexpr int kFrameRate = 10;
constexpr int kFrameCount = 20; // 2 s
constexpr int kFrameIntervalMs = 1000 / kFrameRate;
const QSize kSize(320, 240);
constexpr int kFrameAcceptTimeoutMs = 2000;
constexpr int kFinishTimeoutMs = 10000;
// Each frame paints its index as a horizontal bar position so decoded frames
// can be identified by sampling one pixel row.
constexpr int kBarHeight = 20;
constexpr int kBarStep = 10;

QImage frameAt(int index)
{
    QImage image(kSize, QImage::Format_ARGB32);
    image.fill(Qt::white);
    QPainter painter(&image);
    painter.fillRect(QRect(0, index * kBarStep, kSize.width(), kBarHeight), Qt::black);
    return image;
}

int barRowOf(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        if (qGray(image.pixel(image.width() / 2, y)) < 128) return y;
    }
    return -1;
}

QString createRecording(const QString& path)
{
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) return QStringLiteral("No native encoder");
    encoder->setQuality(IntermediateQuality::kConstantQualityValue);
    encoder->setRateControl(VideoRateControl::ConstantQuality, IntermediateQuality::kConstantQualityValue);
    encoder->setKeyFrameIntervalSeconds(IntermediateQuality::kKeyFrameIntervalSeconds);
    if (!encoder->start(path, kSize, kFrameRate)) return encoder->lastError();
    for (int i = 0; i < kFrameCount; ++i) {
        const qint64 before = encoder->framesWritten();
        QElapsedTimer timer;
        timer.start();
        do {
            encoder->writeFrame(frameAt(i), qint64(i) * kFrameIntervalMs);
            if (encoder->framesWritten() != before) break;
            QTest::qWait(5);
        } while (timer.elapsed() < kFrameAcceptTimeoutMs);
        if (encoder->framesWritten() != before + 1) return QStringLiteral("frame %1 rejected").arg(i);
    }
    QSignalSpy finished(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finished.isEmpty() && !finished.wait(kFinishTimeoutMs)) return QStringLiteral("did not finish");
    return finished.first().at(0).toBool() ? QString() : encoder->lastError();
}

} // namespace

class tst_FrameSource : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void readerDecodesAscendingFrames();
    void readerRejectsDescendingRequests();
    void sourceHonoursTrimAndCrop();
    void sourceFullRangeCountsFrames();
    void openRejectsTinyCrop();
    void openFailsOnMissingFile();

private:
    QTemporaryDir m_dir;
    QString m_path;
};

void tst_FrameSource::initTestCase()
{
    QVERIFY(m_dir.isValid());
    m_path = m_dir.filePath(QStringLiteral("scroll.mp4"));
    const QString error = createRecording(m_path);
    QVERIFY2(error.isEmpty(), qPrintable(error));
}

void tst_FrameSource::readerDecodesAscendingFrames()
{
    auto reader = IVideoFrameReader::create();
    if (!reader) QSKIP("No frame reader on this platform");
    QVERIFY2(reader->load(m_path), qPrintable(reader->lastError()));
    QCOMPARE(reader->videoSize(), kSize);
    QVERIFY(qAbs(reader->frameRate() - kFrameRate) < 0.5);
    QVERIFY(qAbs(reader->duration() - kFrameCount * kFrameIntervalMs) <= kFrameIntervalMs);
    // Decoded H.264 is lossy: the bar row must match within one row.
    for (int i = 0; i < kFrameCount; ++i) {
        const QImage frame = reader->frameAt(qint64(i) * kFrameIntervalMs);
        QVERIFY2(!frame.isNull(), qPrintable(reader->lastError()));
        QCOMPARE(frame.format(), QImage::Format_RGB32);
        QVERIFY2(qAbs(barRowOf(frame) - i * kBarStep) <= 1,
                 qPrintable(QStringLiteral("frame %1 bar at %2").arg(i).arg(barRowOf(frame))));
    }
    // Past the end holds the last frame.
    const QImage tail = reader->frameAt(qint64(kFrameCount + 5) * kFrameIntervalMs);
    QVERIFY(!tail.isNull());
    QVERIFY(qAbs(barRowOf(tail) - (kFrameCount - 1) * kBarStep) <= 1);
}

void tst_FrameSource::readerRejectsDescendingRequests()
{
    auto reader = IVideoFrameReader::create();
    if (!reader) QSKIP("No frame reader on this platform");
    QVERIFY(reader->load(m_path));
    QVERIFY(!reader->frameAt(500).isNull());
    QVERIFY(reader->frameAt(100).isNull());
    QVERIFY(!reader->lastError().isEmpty());
}

void tst_FrameSource::sourceHonoursTrimAndCrop()
{
    auto source = FrameReaderLongshotSource::createNative();
    if (!source) QSKIP("No frame reader on this platform");
    const QRect crop(40, 20, 200, 160); // even-aligned, inside the frame
    QVERIFY2(source->open(m_path, 500, 1200, crop), qPrintable(source->lastError()));
    QCOMPARE(source->frameSize(), crop.size());
    QCOMPARE(source->expectedFrameCount(), 7); // 500,600,...,1100
    int count = 0;
    qint64 lastT = -1;
    while (auto frame = source->next(&lastT)) {
        QCOMPARE(frame->size(), crop.size());
        QVERIFY(lastT >= 500 && lastT < 1200);
        // Frame at t has its bar at row t/100*10 in full-frame coordinates; in
        // crop-local coordinates that is minus crop.y().
        const int expected = int(lastT / kFrameIntervalMs) * kBarStep - crop.y();
        const int actual = barRowOf(*frame);
        if (expected >= 0 && expected + kBarHeight <= crop.height()) {
            QVERIFY2(qAbs(actual - expected) <= 1,
                     qPrintable(QStringLiteral("t=%1 bar %2 expected %3").arg(lastT).arg(actual).arg(expected)));
        }
        ++count;
    }
    QCOMPARE(count, 7);
}

void tst_FrameSource::sourceFullRangeCountsFrames()
{
    auto source = FrameReaderLongshotSource::createNative();
    if (!source) QSKIP("No frame reader on this platform");
    QVERIFY(source->open(m_path, 0, -1, QRect()));
    QCOMPARE(source->frameSize(), kSize);
    int count = 0;
    qint64 t = 0;
    while (source->next(&t)) ++count;
    QCOMPARE(count, kFrameCount);
}

void tst_FrameSource::openRejectsTinyCrop()
{
    auto source = FrameReaderLongshotSource::createNative();
    if (!source) QSKIP("No frame reader on this platform");
    QVERIFY(!source->open(m_path, 0, -1, QRect(0, 0, 32, 32)));
    QVERIFY(source->lastError().contains(QStringLiteral("crop")));
}

void tst_FrameSource::openFailsOnMissingFile()
{
    auto source = FrameReaderLongshotSource::createNative();
    if (!source) QSKIP("No frame reader on this platform");
    QVERIFY(!source->open(m_dir.filePath(QStringLiteral("missing.mp4")), 0, -1, QRect()));
    QVERIFY(!source->lastError().isEmpty());
}

QTEST_MAIN(tst_FrameSource)
#include "tst_FrameSource.moc"
```

`tests/CMakeLists.txt`, after the solver block:

```cmake
add_executable(Longshot_FrameSource Longshot/tst_FrameSource.cpp)
target_include_directories(Longshot_FrameSource PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(Longshot_FrameSource PRIVATE snaptray_platform snaptray_algorithms Qt6::Gui Qt6::Test
    "$<$<PLATFORM_ID:Windows>:mfreadwrite>" "$<$<PLATFORM_ID:Windows>:mfplat>"
    "$<$<PLATFORM_ID:Windows>:mfuuid>" "$<$<PLATFORM_ID:Windows>:ole32>")
add_test(NAME Longshot_FrameSource COMMAND Longshot_FrameSource)
set_tests_properties(Longshot_FrameSource PROPERTIES TIMEOUT 120 LABELS "integration;slow")
```

- [ ] **Step 2: Run the tests to verify they fail**

Build `Longshot_FrameSource`. Expected: `Cannot open include file: 'longshot/FrameReaderLongshotSource.h'`. (On Windows today `IVideoFrameReader::create()` returns nullptr, so even after the headers exist `readerDecodesAscendingFrames` would skip — the MF reader is what turns the skips into assertions.)

- [ ] **Step 3: The frame source interface**

`include/longshot/LongshotFrameSource.h`:

```cpp
#pragma once

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>

#include <optional>

namespace SnapTray::Longshot {

// Sequential, cropped frame access over a trim range of a recording.
// Frames are QImage::Format_RGB32 in crop-local coordinates. Implementations
// decode in media-time order and never hold more than a few frames.
class LongshotFrameSource
{
public:
    virtual ~LongshotFrameSource() = default;

    // startMs inclusive, endMs exclusive (-1 = end of media). `crop` is in
    // video pixels (empty = full frame) and is normalized the same way the
    // preview normalizes its crop (even alignment, minimum side).
    virtual bool open(const QString& path, qint64 startMs, qint64 endMs, const QRect& crop) = 0;
    // Next frame in time order; nullopt at the end of the range or on error
    // (see lastError()). *tMs receives the frame's media time.
    virtual std::optional<QImage> next(qint64* tMs) = 0;
    virtual QSize frameSize() const = 0;        // crop size
    virtual QSize videoSize() const = 0;        // full decoded frame size (for crop normalization keys)
    virtual double frameRate() const = 0;
    virtual int expectedFrameCount() const = 0; // frames in the range at the nominal rate
    virtual QString lastError() const = 0;
};

} // namespace SnapTray::Longshot
```

- [ ] **Step 4: The adapter over IVideoFrameReader**

`include/longshot/FrameReaderLongshotSource.h`:

```cpp
#pragma once

#include "longshot/LongshotFrameSource.h"

#include <memory>

class IVideoFrameReader;

namespace SnapTray::Longshot {

// Adapts the platform IVideoFrameReader (ascending-only frameAt) to the
// longshot contract: iterates media times at the nominal frame rate from
// startMs to endMs and crops each frame.
class FrameReaderLongshotSource final : public LongshotFrameSource
{
public:
    explicit FrameReaderLongshotSource(std::unique_ptr<IVideoFrameReader> reader);
    ~FrameReaderLongshotSource() override;

    // Uses IVideoFrameReader::create(); nullptr where no reader exists (Linux).
    static std::unique_ptr<LongshotFrameSource> createNative();

    bool open(const QString& path, qint64 startMs, qint64 endMs, const QRect& crop) override;
    std::optional<QImage> next(qint64* tMs) override;
    QSize frameSize() const override { return m_crop.size(); }
    QSize videoSize() const override { return m_videoSize; }
    double frameRate() const override { return m_frameRate; }
    int expectedFrameCount() const override { return m_frameCount; }
    QString lastError() const override { return m_lastError; }

private:
    std::unique_ptr<IVideoFrameReader> m_reader;
    QRect m_crop;
    QSize m_videoSize;
    double m_frameRate = 0.0;
    qint64 m_startMs = 0;
    qint64 m_endMs = 0;
    int m_frameCount = 0;
    int m_nextIndex = 0;
    QString m_lastError;
};

} // namespace SnapTray::Longshot
```

`src/longshot/FrameReaderLongshotSource.cpp`:

```cpp
#include "longshot/FrameReaderLongshotSource.h"

#include "longshot/LongshotTypes.h"
#include "utils/VideoCropGeometry.h"
#include "video/IVideoFrameReader.h"

#include <QDebug>

#include <cmath>

namespace SnapTray::Longshot {

namespace {
constexpr double kMsPerSecond = 1000.0;
} // namespace

FrameReaderLongshotSource::FrameReaderLongshotSource(std::unique_ptr<IVideoFrameReader> reader)
    : m_reader(std::move(reader))
{
}

FrameReaderLongshotSource::~FrameReaderLongshotSource() = default;

std::unique_ptr<LongshotFrameSource> FrameReaderLongshotSource::createNative()
{
    auto reader = IVideoFrameReader::create();
    if (!reader) {
        qWarning() << "FrameReaderLongshotSource: no video frame reader on this platform";
        return nullptr;
    }
    return std::make_unique<FrameReaderLongshotSource>(std::move(reader));
}

bool FrameReaderLongshotSource::open(const QString& path, qint64 startMs, qint64 endMs, const QRect& crop)
{
    m_lastError.clear();
    m_nextIndex = 0;
    m_frameCount = 0;
    if (!m_reader) {
        m_lastError = QStringLiteral("No frame reader");
        return false;
    }
    if (!m_reader->load(path)) {
        m_lastError = m_reader->lastError().isEmpty() ? QStringLiteral("Failed to open %1").arg(path)
                                                      : m_reader->lastError();
        qWarning() << "FrameReaderLongshotSource:" << m_lastError;
        return false;
    }
    m_videoSize = m_reader->videoSize();
    const QRect frameRect(QPoint(0, 0), m_videoSize);
    if (!crop.isEmpty() && (crop.width() < kMinAnalysisSide || crop.height() < kMinAnalysisSide)) {
        m_lastError = QStringLiteral("crop %1x%2 is below the minimum %3 px side")
                          .arg(crop.width()).arg(crop.height()).arg(kMinAnalysisSide);
        return false;
    }
    // normalizeCropRect returns an empty rect both for "no crop" and for a
    // crop that covers the whole frame; either way the whole frame is used.
    const QRect normalized = crop.isEmpty() ? QRect() : VideoCropGeometry::normalizeCropRect(crop, m_videoSize);
    m_crop = normalized.isEmpty() ? frameRect : normalized;
    if (m_crop.width() < kMinAnalysisSide || m_crop.height() < kMinAnalysisSide) {
        m_lastError = QStringLiteral("crop %1x%2 is below the minimum %3 px side")
                          .arg(m_crop.width()).arg(m_crop.height()).arg(kMinAnalysisSide);
        return false;
    }
    m_frameRate = m_reader->frameRate() > 0.0 ? m_reader->frameRate() : 30.0;
    const qint64 duration = m_reader->duration();
    m_startMs = qBound<qint64>(0, startMs, duration);
    m_endMs = endMs < 0 ? duration : qBound<qint64>(m_startMs, endMs, duration);
    const double frameIntervalMs = kMsPerSecond / m_frameRate;
    m_frameCount = int(std::ceil(double(m_endMs - m_startMs) / frameIntervalMs));
    if (m_frameCount < 0) m_frameCount = 0;
    return true;
}

std::optional<QImage> FrameReaderLongshotSource::next(qint64* tMs)
{
    if (!m_reader || m_nextIndex >= m_frameCount) return std::nullopt;
    const double frameIntervalMs = kMsPerSecond / m_frameRate;
    const qint64 t = m_startMs + qint64(std::llround(m_nextIndex * frameIntervalMs));
    if (t >= m_endMs) {
        m_nextIndex = m_frameCount;
        return std::nullopt;
    }
    ++m_nextIndex;
    QImage frame = m_reader->frameAt(t);
    if (frame.isNull()) {
        m_lastError = m_reader->lastError();
        qWarning() << "FrameReaderLongshotSource: decode failed at" << t << "ms:" << m_lastError;
        return std::nullopt;
    }
    if (frame.format() != QImage::Format_RGB32) frame = frame.convertToFormat(QImage::Format_RGB32);
    if (m_crop != QRect(QPoint(0, 0), frame.size())) frame = frame.copy(m_crop);
    if (tMs) *tMs = t;
    return frame;
}

} // namespace SnapTray::Longshot
```

Add `src/longshot/FrameReaderLongshotSource.cpp` to the `snaptray_platform` common source list (next to `src/video/IVideoFrameReader.cpp`); `snaptray_platform` already links `snaptray_core` PUBLIC, which supplies `VideoCropGeometry` and the `include/` dir.

- [ ] **Step 5: The Windows reader**

`src/video/MediaFoundationFrameReader_win.cpp`:

```cpp
// Sequential Media Foundation decoder behind IVideoFrameReader. Mirrors the
// macOS AVFoundationFrameReader contract: ascending requests only, empty
// lead-in uses the first frame, gaps hold the last frame. Decodes to RGB32 on
// the CPU (no DXVA) so frames can be copied straight into QImage.
#include "video/IVideoFrameReader.h"
#include "video/MediaFoundationFrameCopy_win.h"

#include <QDebug>

#include <windows.h>
#include <mfapi.h>
#include <mfidl.h>
#include <mfreadwrite.h>
#include <mferror.h>
#include <wrl/client.h>

#include <memory>

using Microsoft::WRL::ComPtr;

namespace {

constexpr LONGLONG kHnsPerMs = 10000;
constexpr double kDefaultFrameRate = 30.0;

qint64 hnsToMs(LONGLONG hns)
{
    return qint64((hns + kHnsPerMs / 2) / kHnsPerMs);
}

class MediaFoundationFrameReader final : public IVideoFrameReader
{
public:
    MediaFoundationFrameReader()
    {
        m_comResult = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
        m_mfResult = MFStartup(MF_VERSION);
    }

    ~MediaFoundationFrameReader() override
    {
        m_reader.Reset();
        if (SUCCEEDED(m_mfResult)) MFShutdown();
        if (SUCCEEDED(m_comResult) || m_comResult == RPC_E_CHANGED_MODE) {
            if (SUCCEEDED(m_comResult)) CoUninitialize();
        }
    }

    bool load(const QString& filePath) override
    {
        m_lastError.clear();
        if (FAILED(m_mfResult)) {
            m_lastError = QStringLiteral("Media Foundation unavailable");
            return false;
        }
        ComPtr<IMFAttributes> attributes;
        HRESULT hr = MFCreateAttributes(&attributes, 3);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SOURCE_READER_ENABLE_VIDEO_PROCESSING, TRUE);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_SOURCE_READER_DISABLE_DXVA, TRUE);
        if (SUCCEEDED(hr)) hr = attributes->SetUINT32(MF_READWRITE_ENABLE_HARDWARE_TRANSFORMS, FALSE);
        if (SUCCEEDED(hr)) {
            hr = MFCreateSourceReaderFromURL(reinterpret_cast<LPCWSTR>(filePath.utf16()), attributes.Get(), &m_reader);
        }
        if (FAILED(hr)) return fail(QStringLiteral("Failed to open %1").arg(filePath), hr);

        hr = m_reader->SetStreamSelection(MF_SOURCE_READER_ALL_STREAMS, FALSE);
        if (SUCCEEDED(hr)) hr = m_reader->SetStreamSelection(MF_SOURCE_READER_FIRST_VIDEO_STREAM, TRUE);
        if (FAILED(hr)) return fail(QStringLiteral("No video stream"), hr);

        ComPtr<IMFMediaType> nativeType;
        hr = m_reader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &nativeType);
        if (FAILED(hr)) return fail(QStringLiteral("No native video type"), hr);
        UINT32 num = 0;
        UINT32 den = 0;
        if (SUCCEEDED(MFGetAttributeRatio(nativeType.Get(), MF_MT_FRAME_RATE, &num, &den)) && den != 0) {
            m_frameRate = double(num) / double(den);
        } else {
            m_frameRate = kDefaultFrameRate;
        }

        ComPtr<IMFMediaType> rgbType;
        hr = MFCreateMediaType(&rgbType);
        if (SUCCEEDED(hr)) hr = rgbType->SetGUID(MF_MT_MAJOR_TYPE, MFMediaType_Video);
        if (SUCCEEDED(hr)) hr = rgbType->SetGUID(MF_MT_SUBTYPE, MFVideoFormat_RGB32);
        if (SUCCEEDED(hr)) hr = m_reader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, rgbType.Get());
        if (FAILED(hr)) return fail(QStringLiteral("RGB32 output not supported"), hr);
        if (!readLayout()) return false;

        PROPVARIANT duration;
        PropVariantInit(&duration);
        hr = m_reader->GetPresentationAttribute(MF_SOURCE_READER_MEDIASOURCE, MF_PD_DURATION, &duration);
        m_durationMs = SUCCEEDED(hr) && duration.vt == VT_UI8 ? hnsToMs(LONGLONG(duration.uhVal.QuadPart)) : 0;
        PropVariantClear(&duration);

        m_current = QImage();
        m_lookAhead = QImage();
        m_lookAheadMs = -1;
        m_lastRequestMs = -1;
        m_endOfStream = false;
        return true;
    }

    QImage frameAt(qint64 positionMs) override
    {
        if (!m_reader) {
            m_lastError = QStringLiteral("No video loaded");
            return {};
        }
        if (positionMs < m_lastRequestMs) {
            m_lastError = QStringLiteral("Video reader requires ascending timestamps");
            return {};
        }
        m_lastRequestMs = positionMs;
        // Advance until the look-ahead frame starts after the requested time;
        // the frame before it is the one displayed at positionMs.
        while (!m_endOfStream && (m_lookAhead.isNull() || m_lookAheadMs <= positionMs)) {
            if (!m_lookAhead.isNull()) {
                m_current = m_lookAhead;
                m_lookAhead = QImage();
            }
            if (!readNext()) return {};
        }
        if (m_current.isNull()) {
            // Lead-in before the first sample: use the first decoded frame.
            if (m_lookAhead.isNull()) {
                m_lastError = m_lastError.isEmpty() ? QStringLiteral("No video frames") : m_lastError;
                return {};
            }
            return m_lookAhead;
        }
        return m_current;
    }

    QSize videoSize() const override { return m_size; }
    qint64 duration() const override { return m_durationMs; }
    double frameRate() const override { return m_frameRate; }
    QString lastError() const override { return m_lastError; }

private:
    bool fail(const QString& message, HRESULT hr)
    {
        m_lastError = QStringLiteral("%1 (hr=0x%2)").arg(message).arg(ulong(hr), 8, 16, QChar('0'));
        qWarning() << "MediaFoundationFrameReader:" << m_lastError;
        m_reader.Reset();
        return false;
    }

    bool readLayout()
    {
        ComPtr<IMFMediaType> type;
        HRESULT hr = m_reader->GetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, &type);
        if (FAILED(hr)) return fail(QStringLiteral("No current media type"), hr);
        UINT32 width = 0;
        UINT32 height = 0;
        hr = MFGetAttributeSize(type.Get(), MF_MT_FRAME_SIZE, &width, &height);
        if (FAILED(hr) || width == 0 || height == 0) return fail(QStringLiteral("Invalid frame size"), hr);
        // Honour the display aperture when the decoder pads to macroblocks.
        MFVideoArea area{};
        UINT32 blobSize = 0;
        m_origin = QPoint(0, 0);
        m_size = QSize(int(width), int(height));
        if (SUCCEEDED(type->GetBlob(MF_MT_MINIMUM_DISPLAY_APERTURE, reinterpret_cast<UINT8*>(&area), sizeof(area), &blobSize))
            && blobSize == sizeof(area) && area.Area.cx > 0 && area.Area.cy > 0) {
            m_origin = QPoint(area.OffsetX.value, area.OffsetY.value);
            m_size = QSize(area.Area.cx, area.Area.cy);
        }
        UINT32 stride = 0;
        if (SUCCEEDED(type->GetUINT32(MF_MT_DEFAULT_STRIDE, &stride))) {
            m_defaultStride = LONG(stride);
        } else {
            m_defaultStride = LONG(width) * 4;
        }
        return true;
    }

    // Decodes one sample into m_lookAhead. Returns false on error.
    bool readNext()
    {
        for (;;) {
            DWORD flags = 0;
            LONGLONG timestamp = 0;
            ComPtr<IMFSample> sample;
            const HRESULT hr = m_reader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, nullptr, &flags, &timestamp, &sample);
            if (FAILED(hr)) return fail(QStringLiteral("ReadSample failed"), hr);
            if (flags & (MF_SOURCE_READERF_CURRENTMEDIATYPECHANGED | MF_SOURCE_READERF_NATIVEMEDIATYPECHANGED)) {
                if (!readLayout()) return false;
            }
            if (flags & MF_SOURCE_READERF_ENDOFSTREAM) {
                m_endOfStream = true;
                return true;
            }
            if (!sample) continue; // stream tick or gap: keep reading
            QImage frame = copySample(sample.Get());
            if (frame.isNull()) return false;
            m_lookAhead = frame;
            m_lookAheadMs = hnsToMs(timestamp);
            return true;
        }
    }

    QImage copySample(IMFSample* sample)
    {
        ComPtr<IMFMediaBuffer> buffer;
        HRESULT hr = sample->ConvertToContiguousBuffer(&buffer);
        if (FAILED(hr)) {
            fail(QStringLiteral("No sample buffer"), hr);
            return {};
        }
        ComPtr<IMF2DBuffer> buffer2d;
        BYTE* scanline0 = nullptr;
        LONG pitch = 0;
        QImage frame;
        if (SUCCEEDED(buffer.As(&buffer2d)) && SUCCEEDED(buffer2d->Lock2D(&scanline0, &pitch))) {
            const uchar* origin = scanline0 + qsizetype(m_origin.y()) * pitch + qsizetype(m_origin.x()) * 4;
            frame = copyMediaFoundationRgb32Frame(origin, pitch, m_size);
            buffer2d->Unlock2D();
        } else {
            BYTE* data = nullptr;
            DWORD length = 0;
            hr = buffer->Lock(&data, nullptr, &length);
            if (FAILED(hr)) {
                fail(QStringLiteral("Buffer lock failed"), hr);
                return {};
            }
            // Positive default stride = top-down, negative = bottom-up (RGB32 is bottom-up by default).
            const LONG stride = m_defaultStride;
            const uchar* firstRow = stride >= 0 ? data : data + qsizetype(length) + stride; // last row start
            const uchar* origin = firstRow + qsizetype(m_origin.y()) * stride + qsizetype(m_origin.x()) * 4;
            frame = copyMediaFoundationRgb32Frame(origin, stride, m_size);
            buffer->Unlock();
        }
        return frame;
    }

    HRESULT m_comResult = E_FAIL;
    HRESULT m_mfResult = E_FAIL;
    ComPtr<IMFSourceReader> m_reader;
    QSize m_size;
    QPoint m_origin;
    LONG m_defaultStride = 0;
    double m_frameRate = kDefaultFrameRate;
    qint64 m_durationMs = 0;
    QImage m_current;
    QImage m_lookAhead;
    qint64 m_lookAheadMs = -1;
    qint64 m_lastRequestMs = -1;
    bool m_endOfStream = false;
    QString m_lastError;
};

} // namespace

std::unique_ptr<IVideoFrameReader> createMediaFoundationFrameReader()
{
    return std::make_unique<MediaFoundationFrameReader>();
}
```

`src/video/IVideoFrameReader.cpp`: add `std::unique_ptr<IVideoFrameReader> createMediaFoundationFrameReader();` declaration under `#ifdef Q_OS_WIN` and make `create()` return it on Windows (keep macOS and the Linux nullptr). Add `src/video/MediaFoundationFrameReader_win.cpp` to the Windows-only `snaptray_platform` source list next to `src/video/MediaFoundationTranscoder_win.cpp`. In `include/video/IVideoFrameReader.h`, extend the class comment: "Windows: MediaFoundationFrameReader (RGB32, CPU decode)". Read `MediaFoundationFrameCopy_win.h` before using it: it expects `scanline0` to be the first *displayed* row and accepts a negative pitch; the bottom-up branch above derives the first row from `length + stride` — verify against `MediaFoundationPlayer_win.cpp`'s equivalent and copy its arithmetic if it differs.

- [ ] **Step 6: Run the tests to verify they pass**

Build and run `Longshot_FrameSource` (`-o <scratch>/t2.txt,txt`). Expected: 6 slots pass on Windows (none skipped). If `readerDecodesAscendingFrames` reports every bar one row off in the same direction, the aperture origin or bottom-up arithmetic is wrong; fix the reader, not the tolerance. Also rebuild and run `Qml_RecordingPreviewExport` — `performFormatConversion` must still take its player path on Windows (it tests `IVideoFrameReader::create()` first on macOS only; confirm by reading the code; if it now picks the new reader on Windows, that is acceptable only if the GIF/WebP tests stay green).

- [ ] **Step 7: Commit**

```bash
git add include/longshot/LongshotFrameSource.h include/longshot/FrameReaderLongshotSource.h src/longshot/FrameReaderLongshotSource.cpp src/video/MediaFoundationFrameReader_win.cpp src/video/IVideoFrameReader.cpp include/video/IVideoFrameReader.h tests/Longshot/tst_FrameSource.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(longshot): add a Windows frame reader and the trim-aware frame source"
```

---
### Task 3: Synthetic harness

**Files:**
- Create: `tests/Longshot/SyntheticScroll.h`, `tests/Longshot/SyntheticScroll.cpp`
- Create: `tests/Longshot/tst_SyntheticHarness.cpp`
- Modify: `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `IVideoEncoder::createNativeEncoder()` + `setRateControl`, `FrameReaderLongshotSource::createNative()` (Task 2).
- Produces (namespace `SyntheticScroll`, test-only): `PageSpec`, `QImage renderPage(const PageSpec&)`, `Trajectory` (`std::vector<int> offsets`) builders `constantSpeed`, `fling`, `backAndForth`, `withPauses`, `clampTrajectory`; `Disturbances`; `QImage renderFrame(const QImage& page, const QSize& viewport, const Trajectory& trajectory, int frameIndex, const Disturbances&)`; `QString encodeFrames(const QString& path, const std::vector<QImage>& frames, int frameRate)`; `RowMatchReport compareWithGroundTruth(const QImage& result, const QImage& page, int firstPageRow, int lastPageRow)`; constants `kViewport(640, 480)`, `kFrameRate = 20`. Tasks 5–6 drive the engine with these.

- [ ] **Step 1: Write the harness header**

`tests/Longshot/SyntheticScroll.h`:

```cpp
#pragma once

#include <QImage>
#include <QSize>
#include <QString>

#include <vector>

// Test-only synthetic scrolling corpus: a tall ground-truth page, scroll
// trajectories, on-screen disturbances, real-encoder recordings and
// row-level scoring of a stitched result.
namespace SyntheticScroll {

const QSize kViewport(640, 480);
constexpr int kFrameRate = 20;

struct PageSpec {
    int width = kViewport.width();
    int height = 4000;
    quint32 seed = 1;
    // Rows [blankTop, blankTop + blankHeight) are left uniform white; 0 = none.
    int blankTop = 0;
    int blankHeight = 0;
};

struct Trajectory {
    std::vector<int> offsets; // page row shown at the top of the viewport, per frame
};

struct Disturbances {
    int stickyHeaderHeight = 0;     // always-on opaque header at the top of the viewport
    int scrollUpHeaderHeight = 0;   // header that appears only on frames where the offset decreased
    int sidebarWidth = 0;           // opaque static panel on the right edge
    bool hoverChange = false;       // a block changes colour on every third frame
    int lazyLoadRow = -1;           // page row of a 200-row block that is grey until lazyLoadFrame
    int lazyLoadFrame = 0;
};

struct RowMatchReport {
    int outputRows = 0;
    int matchedRows = 0;     // output rows that matched some page row
    int duplicatedRows = 0;  // output rows mapping to a page row already used by the previous output row
    int missingRows = 0;     // page rows in [first, last] no output row mapped to
    int misalignedRows = 0;  // matched rows whose page row breaks monotonic +1 progression by more than 1
    int unmatchedRows = 0;   // output rows with no acceptable page match
};

QImage renderPage(const PageSpec& spec);

Trajectory constantSpeed(int frameCount, int startOffset, int pixelsPerFrame);
Trajectory fling(int frameCount, int startOffset, int peakPixelsPerFrame); // fast start, decelerating to 0
Trajectory backAndForth(int frameCount, int startOffset, int amplitude, int pixelsPerFrame);
Trajectory withPauses(const Trajectory& base, int pauseEveryFrames, int pauseFrames);
Trajectory clampTrajectory(const Trajectory& trajectory, int pageHeight, int viewportHeight);

// Frame `frameIndex` of the recording: the viewport over the page at
// trajectory.offsets[frameIndex] with the disturbances painted on top.
QImage renderFrame(const QImage& page, const QSize& viewport, const Trajectory& trajectory, int frameIndex,
                   const Disturbances& disturbances);

// Encodes with the native encoder at intermediate quality (constant quality
// request, 1 s keyframes). Returns an empty string on success, else an error.
QString encodeFrames(const QString& path, const std::vector<QImage>& frames, int frameRate);

// Maps each output row to the best-matching ground-truth row (by a 64-bin
// luma profile within a search window around the previous match) and scores
// the mapping. [firstPageRow, lastPageRow] is the page span the recording
// actually showed, so rows outside it are not counted as missing.
RowMatchReport compareWithGroundTruth(const QImage& result, const QImage& page, int firstPageRow, int lastPageRow);

} // namespace SyntheticScroll
```

- [ ] **Step 2: Write the failing harness self-test**

`tests/Longshot/tst_SyntheticHarness.cpp` (checks that the harness is a trustworthy oracle before any engine test relies on it):

```cpp
#include "SyntheticScroll.h"
#include "longshot/FrameReaderLongshotSource.h"

#include <QtTest>
#include <QTemporaryDir>

using namespace SyntheticScroll;

namespace {

// Mean absolute luma difference between two same-sized images, 0..255.
double meanAbsDiff(const QImage& a, const QImage& b)
{
    Q_ASSERT(a.size() == b.size());
    double sum = 0.0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x) sum += qAbs(qGray(a.pixel(x, y)) - qGray(b.pixel(x, y)));
    }
    return sum / (double(a.width()) * a.height());
}

// Loose bound for H.264 at intermediate quality on text-like content.
constexpr double kMaxCodecError = 6.0;

} // namespace

class tst_SyntheticHarness : public QObject
{
    Q_OBJECT
private slots:
    void pageIsDeterministicAndBusy();
    void trajectoriesClamp();
    void frameIsPageWindow();
    void disturbancesPaintWhereExpected();
    void compareIdenticalIsClean();
    void compareDetectsDuplicatesMissingAndShift();
    void encodedFramesDecodeCloseToSource();
};

void tst_SyntheticHarness::pageIsDeterministicAndBusy()
{
    PageSpec spec;
    const QImage a = renderPage(spec);
    const QImage b = renderPage(spec);
    QCOMPARE(a, b);
    QCOMPARE(a.size(), QSize(spec.width, spec.height));
    // Every 64-row band must contain ink, so no viewport-sized blank exists by default.
    for (int y = 0; y + 64 <= a.height(); y += 64) {
        int dark = 0;
        for (int yy = y; yy < y + 64; ++yy) {
            for (int x = 0; x < a.width(); x += 8) dark += qGray(a.pixel(x, yy)) < 128;
        }
        QVERIFY2(dark > 0, qPrintable(QStringLiteral("blank band at %1").arg(y)));
    }
    spec.seed = 2;
    QVERIFY(renderPage(spec) != a);
}

void tst_SyntheticHarness::trajectoriesClamp()
{
    const Trajectory t = clampTrajectory(constantSpeed(50, 3900, 40), 4000, kViewport.height());
    QCOMPARE(t.offsets.size(), size_t(50));
    for (int o : t.offsets) QVERIFY(o >= 0 && o <= 4000 - kViewport.height());
    const Trajectory f = fling(30, 0, 120);
    QVERIFY(f.offsets[1] - f.offsets[0] >= f.offsets[29] - f.offsets[28]);
    QCOMPARE(f.offsets[29], f.offsets[28]); // settles
    const Trajectory b = backAndForth(40, 500, 300, 30);
    QVERIFY(*std::max_element(b.offsets.begin(), b.offsets.end()) <= 800);
    QVERIFY(*std::min_element(b.offsets.begin(), b.offsets.end()) >= 200);
    const Trajectory p = withPauses(constantSpeed(10, 0, 10), 3, 2);
    QCOMPARE(p.offsets.size(), size_t(10 + 3 * 2)); // a pause after frames 3, 6, 9
    QCOMPARE(p.offsets[3], p.offsets[4]);
}

void tst_SyntheticHarness::frameIsPageWindow()
{
    const QImage page = renderPage(PageSpec{});
    const Trajectory t = constantSpeed(3, 100, 50);
    const QImage frame = renderFrame(page, kViewport, t, 2, Disturbances{});
    QCOMPARE(frame.size(), kViewport);
    QCOMPARE(frame.format(), QImage::Format_RGB32);
    QCOMPARE(meanAbsDiff(frame, page.copy(0, 200, kViewport.width(), kViewport.height()).convertToFormat(QImage::Format_RGB32)), 0.0);
}

void tst_SyntheticHarness::disturbancesPaintWhereExpected()
{
    const QImage page = renderPage(PageSpec{});
    Disturbances d;
    d.stickyHeaderHeight = 40;
    d.sidebarWidth = 100;
    const Trajectory t = constantSpeed(3, 100, 50);
    const QImage frame = renderFrame(page, kViewport, t, 1, d);
    // Header and sidebar are opaque and identical regardless of offset.
    const QImage other = renderFrame(page, kViewport, t, 2, d);
    QCOMPARE(frame.copy(0, 0, kViewport.width(), 40), other.copy(0, 0, kViewport.width(), 40));
    QCOMPARE(frame.copy(kViewport.width() - 100, 0, 100, kViewport.height()),
             other.copy(kViewport.width() - 100, 0, 100, kViewport.height()));
    // Scroll-up header only on frames whose offset decreased.
    Disturbances up;
    up.scrollUpHeaderHeight = 48;
    const Trajectory bf = backAndForth(6, 400, 200, 100); // goes down then up
    int firstUpFrame = -1;
    for (size_t i = 1; i < bf.offsets.size(); ++i) {
        if (bf.offsets[i] < bf.offsets[i - 1]) { firstUpFrame = int(i); break; }
    }
    QVERIFY(firstUpFrame > 0);
    const QImage down = renderFrame(page, kViewport, bf, 1, up);
    const QImage upFrame = renderFrame(page, kViewport, bf, firstUpFrame, up);
    QVERIFY(down.copy(0, 0, kViewport.width(), 48) != upFrame.copy(0, 0, kViewport.width(), 48));
    QCOMPARE(upFrame.copy(0, 0, kViewport.width(), 48),
             renderFrame(page, kViewport, bf, firstUpFrame, up).copy(0, 0, kViewport.width(), 48));
}

void tst_SyntheticHarness::compareIdenticalIsClean()
{
    const QImage page = renderPage(PageSpec{});
    const QImage slice = page.copy(0, 500, page.width(), 1000);
    const RowMatchReport r = compareWithGroundTruth(slice, page, 500, 1499);
    QCOMPARE(r.outputRows, 1000);
    QCOMPARE(r.matchedRows, 1000);
    QCOMPARE(r.duplicatedRows, 0);
    QCOMPARE(r.missingRows, 0);
    QCOMPARE(r.misalignedRows, 0);
    QCOMPARE(r.unmatchedRows, 0);
}

void tst_SyntheticHarness::compareDetectsDuplicatesMissingAndShift()
{
    const QImage page = renderPage(PageSpec{});
    // Duplicate 10 rows: copy rows 500-999 then 990-1499.
    QImage dup(page.width(), 1010, QImage::Format_RGB32);
    QPainter p(&dup);
    p.drawImage(0, 0, page, 0, 500, page.width(), 500);
    p.drawImage(0, 500, page, 0, 990, page.width(), 510);
    p.end();
    RowMatchReport r = compareWithGroundTruth(dup, page, 500, 1499);
    QVERIFY2(r.duplicatedRows >= 9 && r.duplicatedRows <= 11, qPrintable(QString::number(r.duplicatedRows)));
    QCOMPARE(r.missingRows, 0);
    // Skip 10 rows: rows 500-999 then 1010-1499.
    QImage gap(page.width(), 990, QImage::Format_RGB32);
    QPainter g(&gap);
    g.drawImage(0, 0, page, 0, 500, page.width(), 500);
    g.drawImage(0, 500, page, 0, 1010, page.width(), 490);
    g.end();
    r = compareWithGroundTruth(gap, page, 500, 1499);
    QVERIFY2(r.missingRows >= 9 && r.missingRows <= 11, qPrintable(QString::number(r.missingRows)));
    QCOMPARE(r.duplicatedRows, 0);
    // A corrupted row (inverted) is not a page row: it must count as unmatched, not be forced onto a neighbour.
    QImage corrupted = page.copy(0, 500, page.width(), 200);
    QImage row = corrupted.copy(0, 100, page.width(), 1);
    row.invertPixels();
    QPainter s(&corrupted);
    s.drawImage(0, 100, row);
    s.end();
    r = compareWithGroundTruth(corrupted, page, 500, 699);
    QCOMPARE(r.unmatchedRows, 1);
    QVERIFY(r.matchedRows >= 199);
    QCOMPARE(r.duplicatedRows, 0);
}

void tst_SyntheticHarness::encodedFramesDecodeCloseToSource()
{
    auto source = SnapTray::Longshot::FrameReaderLongshotSource::createNative();
    if (!source) QSKIP("No frame reader on this platform");
    QTemporaryDir dir;
    QVERIFY(dir.isValid());
    const QImage page = renderPage(PageSpec{});
    const Trajectory t = clampTrajectory(constantSpeed(40, 0, 30), page.height(), kViewport.height());
    std::vector<QImage> frames;
    for (int i = 0; i < 40; ++i) frames.push_back(renderFrame(page, kViewport, t, i, Disturbances{}));
    const QString path = dir.filePath(QStringLiteral("constant.mp4"));
    const QString error = encodeFrames(path, frames, kFrameRate);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(source->open(path, 0, -1, QRect()), qPrintable(source->lastError()));
    int index = 0;
    qint64 tMs = 0;
    while (auto decoded = source->next(&tMs)) {
        QVERIFY(index < 40);
        const double err = meanAbsDiff(*decoded, frames[index]);
        QVERIFY2(err < kMaxCodecError, qPrintable(QStringLiteral("frame %1 error %2").arg(index).arg(err)));
        ++index;
    }
    QCOMPARE(index, 40);
}

QTEST_MAIN(tst_SyntheticHarness)
#include "tst_SyntheticHarness.moc"
```

`tests/CMakeLists.txt`:

```cmake
add_library(Longshot_SyntheticScroll STATIC Longshot/SyntheticScroll.cpp Longshot/SyntheticScroll.h)
target_include_directories(Longshot_SyntheticScroll PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/Longshot)
target_link_libraries(Longshot_SyntheticScroll PUBLIC snaptray_platform snaptray_algorithms Qt6::Gui Qt6::Test)

add_executable(Longshot_SyntheticHarness Longshot/tst_SyntheticHarness.cpp)
target_link_libraries(Longshot_SyntheticHarness PRIVATE Longshot_SyntheticScroll Qt6::Test
    "$<$<PLATFORM_ID:Windows>:mfreadwrite>" "$<$<PLATFORM_ID:Windows>:mfplat>"
    "$<$<PLATFORM_ID:Windows>:mfuuid>" "$<$<PLATFORM_ID:Windows>:ole32>")
add_test(NAME Longshot_SyntheticHarness COMMAND Longshot_SyntheticHarness)
set_tests_properties(Longshot_SyntheticHarness PROPERTIES TIMEOUT 180 LABELS "integration;slow")
```

- [ ] **Step 3: Run the test to verify it fails**

Build `Longshot_SyntheticHarness`. Expected: link errors for every `SyntheticScroll::` function (header exists, no definitions yet) — or a compile error if you built the header after the test; either is the RED state.

- [ ] **Step 4: Implement the harness**

`tests/Longshot/SyntheticScroll.cpp`:

```cpp
#include "SyntheticScroll.h"

#include "IVideoEncoder.h"
#include "encoding/IntermediateQuality.h"
#include "encoding/VideoRateControl.h"

#include <QElapsedTimer>
#include <QPainter>
#include <QRandomGenerator>
#include <QSignalSpy>
#include <QtTest>

#include <algorithm>
#include <cmath>
#include <memory>

namespace SyntheticScroll {

namespace {

// Page content: "text lines" of random dark rectangles with a line pitch,
// headings, horizontal rules and coloured blocks. Dense enough that every
// 64-row band has ink unless a blank band is requested.
constexpr int kLinePitch = 22;
constexpr int kLineHeight = 12;
constexpr int kMargin = 24;
constexpr int kMinWordWidth = 18;
constexpr int kMaxWordWidth = 90;
constexpr int kWordGap = 8;
constexpr int kHeadingEvery = 9;        // every ninth line is a heading
constexpr int kRuleEvery = 23;          // horizontal rule every 23 lines
constexpr int kBlockEvery = 31;         // coloured block every 31 lines
constexpr int kBlockHeight = 60;
constexpr int kLazyBlockHeight = 200;
constexpr int kHoverBlockHeight = 40;
constexpr int kHoverBlockWidth = 160;
constexpr int kHoverEveryFrames = 3;

constexpr int kFrameAcceptTimeoutMs = 2000;
constexpr int kFinishTimeoutMs = 10000;
constexpr int kBackpressurePollMs = 5;

// Row profile for matching: luma summed into 64 column bins.
constexpr int kProfileBins = 64;
constexpr double kProfileTolerance = 6.0;   // mean abs bin difference, 0..255
constexpr int kSearchWindowRows = 240;      // around the previous match
constexpr int kInitialSearchRows = 4000;    // first row: search the whole span

const QColor kInk(30, 30, 30);
const QColor kHeading(10, 40, 120);
const QColor kRule(180, 180, 180);
const QColor kBlockColours[] = {QColor(230, 120, 60), QColor(60, 160, 90), QColor(90, 110, 220)};
const QColor kHeader(245, 245, 250);
const QColor kHeaderInk(60, 60, 80);
const QColor kSidebar(235, 238, 242);
const QColor kLazyPlaceholder(200, 200, 200);
const QColor kHoverA(250, 220, 100);
const QColor kHoverB(100, 200, 250);

std::vector<float> rowProfile(const QImage& image, int y)
{
    std::vector<float> bins(kProfileBins, 0.0f);
    std::vector<int> counts(kProfileBins, 0);
    const uchar* line = image.constScanLine(y);
    for (int x = 0; x < image.width(); ++x) {
        const QRgb px = reinterpret_cast<const QRgb*>(line)[x];
        const int bin = int(qint64(x) * kProfileBins / image.width());
        bins[bin] += float(qGray(px));
        ++counts[bin];
    }
    for (int b = 0; b < kProfileBins; ++b) {
        if (counts[b] > 0) bins[b] /= float(counts[b]);
    }
    return bins;
}

double profileDistance(const std::vector<float>& a, const std::vector<float>& b)
{
    double sum = 0.0;
    for (int i = 0; i < kProfileBins; ++i) sum += std::abs(a[i] - b[i]);
    return sum / kProfileBins;
}

} // namespace

QImage renderPage(const PageSpec& spec)
{
    QImage page(spec.width, spec.height, QImage::Format_RGB32);
    page.fill(Qt::white);
    QPainter painter(&page);
    QRandomGenerator random(spec.seed);
    int line = 0;
    for (int y = kMargin; y + kLineHeight <= spec.height - kMargin; y += kLinePitch, ++line) {
        if (spec.blankHeight > 0 && y + kLineHeight > spec.blankTop && y < spec.blankTop + spec.blankHeight) {
            continue;
        }
        if (line % kBlockEvery == kBlockEvery - 1) {
            painter.fillRect(QRect(kMargin, y, spec.width - 2 * kMargin, kBlockHeight),
                             kBlockColours[line % 3]);
            y += kBlockHeight - kLinePitch; // the loop adds one pitch
            continue;
        }
        if (line % kRuleEvery == kRuleEvery - 1) {
            painter.fillRect(QRect(kMargin, y + kLineHeight / 2, spec.width - 2 * kMargin, 2), kRule);
            continue;
        }
        const bool heading = line % kHeadingEvery == 0;
        const int height = heading ? kLineHeight + 6 : kLineHeight;
        const QColor colour = heading ? kHeading : kInk;
        int x = kMargin + int(random.bounded(40));
        const int lineEnd = spec.width - kMargin - int(random.bounded(120));
        while (x < lineEnd) {
            const int width = kMinWordWidth + int(random.bounded(kMaxWordWidth - kMinWordWidth));
            painter.fillRect(QRect(x, y, std::min(width, lineEnd - x), height), colour);
            x += width + kWordGap;
        }
    }
    return page;
}

Trajectory constantSpeed(int frameCount, int startOffset, int pixelsPerFrame)
{
    Trajectory t;
    for (int i = 0; i < frameCount; ++i) t.offsets.push_back(startOffset + i * pixelsPerFrame);
    return t;
}

Trajectory fling(int frameCount, int startOffset, int peakPixelsPerFrame)
{
    Trajectory t;
    double offset = startOffset;
    double speed = peakPixelsPerFrame;
    const double decay = 0.82;
    for (int i = 0; i < frameCount; ++i) {
        t.offsets.push_back(int(std::lround(offset)));
        offset += speed;
        speed = speed * decay;
        if (speed < 0.5) speed = 0.0;
    }
    return t;
}

Trajectory backAndForth(int frameCount, int startOffset, int amplitude, int pixelsPerFrame)
{
    Trajectory t;
    int offset = startOffset;
    int direction = 1;
    for (int i = 0; i < frameCount; ++i) {
        t.offsets.push_back(offset);
        offset += direction * pixelsPerFrame;
        if (offset >= startOffset + amplitude) { offset = startOffset + amplitude; direction = -1; }
        if (offset <= startOffset - amplitude) { offset = startOffset - amplitude; direction = 1; }
    }
    return t;
}

Trajectory withPauses(const Trajectory& base, int pauseEveryFrames, int pauseFrames)
{
    Trajectory t;
    for (size_t i = 0; i < base.offsets.size(); ++i) {
        t.offsets.push_back(base.offsets[i]);
        if ((int(i) + 1) % pauseEveryFrames == 0) {
            for (int p = 0; p < pauseFrames; ++p) t.offsets.push_back(base.offsets[i]);
        }
    }
    return t;
}

Trajectory clampTrajectory(const Trajectory& trajectory, int pageHeight, int viewportHeight)
{
    Trajectory t = trajectory;
    const int maxOffset = std::max(0, pageHeight - viewportHeight);
    for (int& o : t.offsets) o = std::clamp(o, 0, maxOffset);
    return t;
}

QImage renderFrame(const QImage& page, const QSize& viewport, const Trajectory& trajectory, int frameIndex,
                   const Disturbances& d)
{
    const int offset = trajectory.offsets.at(size_t(frameIndex));
    QImage frame(viewport, QImage::Format_RGB32);
    frame.fill(Qt::white);
    QPainter painter(&frame);
    // Lazy-load placeholder is part of the page until lazyLoadFrame.
    QImage source = page;
    if (d.lazyLoadRow >= 0 && frameIndex < d.lazyLoadFrame) {
        QPainter cover(&source);
        cover.fillRect(QRect(0, d.lazyLoadRow, page.width(), kLazyBlockHeight), kLazyPlaceholder);
    }
    painter.drawImage(0, 0, source, 0, offset, viewport.width(), viewport.height());
    if (d.hoverChange) {
        // A fixed page element (row 900) toggles colour every third frame.
        const int pageRow = 900;
        const int y = pageRow - offset;
        if (y + kHoverBlockHeight > 0 && y < viewport.height()) {
            painter.fillRect(QRect(kMargin, y, kHoverBlockWidth, kHoverBlockHeight),
                             (frameIndex % kHoverEveryFrames == 0) ? kHoverA : kHoverB);
        }
    }
    if (d.sidebarWidth > 0) {
        painter.fillRect(QRect(viewport.width() - d.sidebarWidth, 0, d.sidebarWidth, viewport.height()), kSidebar);
        for (int y = kMargin; y < viewport.height(); y += 2 * kLinePitch) {
            painter.fillRect(QRect(viewport.width() - d.sidebarWidth + 12, y, d.sidebarWidth - 24, kLineHeight), kHeaderInk);
        }
    }
    if (d.stickyHeaderHeight > 0) {
        painter.fillRect(QRect(0, 0, viewport.width(), d.stickyHeaderHeight), kHeader);
        painter.fillRect(QRect(kMargin, d.stickyHeaderHeight / 3, 200, d.stickyHeaderHeight / 3), kHeaderInk);
    }
    if (d.scrollUpHeaderHeight > 0 && frameIndex > 0
        && trajectory.offsets.at(size_t(frameIndex)) < trajectory.offsets.at(size_t(frameIndex - 1))) {
        painter.fillRect(QRect(0, 0, viewport.width(), d.scrollUpHeaderHeight), kHeader);
        painter.fillRect(QRect(kMargin, d.scrollUpHeaderHeight / 3, 260, d.scrollUpHeaderHeight / 3), kHeading);
    }
    return frame;
}

QString encodeFrames(const QString& path, const std::vector<QImage>& frames, int frameRate)
{
    if (frames.empty()) return QStringLiteral("no frames");
    std::unique_ptr<IVideoEncoder> encoder(IVideoEncoder::createNativeEncoder());
    if (!encoder) return QStringLiteral("No native encoder");
    using namespace SnapTray;
    encoder->setQuality(IntermediateQuality::kConstantQualityValue);
    encoder->setRateControl(VideoRateControl::ConstantQuality, IntermediateQuality::kConstantQualityValue);
    encoder->setKeyFrameIntervalSeconds(IntermediateQuality::kKeyFrameIntervalSeconds);
    if (!encoder->start(path, frames.front().size(), frameRate)) return encoder->lastError();
    for (size_t i = 0; i < frames.size(); ++i) {
        const qint64 before = encoder->framesWritten();
        QElapsedTimer timer;
        timer.start();
        do {
            encoder->writeFrame(frames[i], qint64(i) * 1000 / frameRate);
            if (encoder->framesWritten() != before) break;
            QTest::qWait(kBackpressurePollMs);
        } while (timer.elapsed() < kFrameAcceptTimeoutMs);
        if (encoder->framesWritten() != before + 1) return QStringLiteral("frame %1 rejected").arg(i);
    }
    QSignalSpy finished(encoder.get(), &IVideoEncoder::finished);
    encoder->finish();
    if (finished.isEmpty() && !finished.wait(kFinishTimeoutMs)) return QStringLiteral("encoder did not finish");
    return finished.first().at(0).toBool() ? QString() : encoder->lastError();
}

RowMatchReport compareWithGroundTruth(const QImage& result, const QImage& page, int firstPageRow, int lastPageRow)
{
    RowMatchReport report;
    report.outputRows = result.height();
    if (result.isNull() || page.isNull()) return report;
    const QImage out = result.convertToFormat(QImage::Format_RGB32);
    const QImage truth = page.convertToFormat(QImage::Format_RGB32);
    std::vector<std::vector<float>> truthProfiles(truth.height());
    for (int y = 0; y < truth.height(); ++y) truthProfiles[y] = rowProfile(truth, y);

    std::vector<int> mapping(out.height(), -1);
    int previous = -1;
    for (int y = 0; y < out.height(); ++y) {
        const std::vector<float> profile = rowProfile(out, y);
        const int centre = previous < 0 ? (firstPageRow + lastPageRow) / 2 : previous + 1;
        const int radius = previous < 0 ? kInitialSearchRows : kSearchWindowRows;
        const int lo = std::max(0, centre - radius);
        const int hi = std::min(truth.height() - 1, centre + radius);
        int best = -1;
        double bestDistance = kProfileTolerance;
        for (int p = lo; p <= hi; ++p) {
            const double distance = profileDistance(profile, truthProfiles[p]);
            // Prefer the row closest to the expected progression on ties.
            if (distance < bestDistance || (best >= 0 && distance == bestDistance && std::abs(p - centre) < std::abs(best - centre))) {
                bestDistance = distance;
                best = p;
            }
        }
        mapping[y] = best;
        if (best < 0) {
            ++report.unmatchedRows;
        } else {
            ++report.matchedRows;
            if (previous >= 0 && best == previous) ++report.duplicatedRows;
            else if (previous >= 0 && std::abs(best - (previous + 1)) > 1 && best != previous) ++report.misalignedRows;
            previous = best;
        }
    }
    std::vector<bool> covered(truth.height(), false);
    for (int p : mapping) {
        if (p >= 0) covered[p] = true;
    }
    for (int p = std::max(0, firstPageRow); p <= std::min(truth.height() - 1, lastPageRow); ++p) {
        if (!covered[p]) ++report.missingRows;
    }
    return report;
}

} // namespace SyntheticScroll
```

- [ ] **Step 5: Run the test to verify it passes**

Build and run `Longshot_SyntheticHarness` (`-o <scratch>/t3.txt,txt`). Expected: 7 slots pass. If `compareDetectsDuplicatesMissingAndShift` counts duplicates outside 9–11, the issue is identical adjacent page rows in the generated page (two consecutive blank rows between lines map to the same profile): the matcher prefers the row nearest the expected progression on ties, so adjacent identical rows still advance — confirm the tie rule before changing bounds. If `encodedFramesDecodeCloseToSource` exceeds the codec error on this machine's encoder, raise `kMaxCodecError` to at most 10 and record the measured value in the report.

- [ ] **Step 6: Commit**

```bash
git add tests/Longshot/SyntheticScroll.h tests/Longshot/SyntheticScroll.cpp tests/Longshot/tst_SyntheticHarness.cpp tests/CMakeLists.txt
git commit -m "test(longshot): add the synthetic scrolling harness"
```

---
### Task 4: LongshotAnalyzer — features, shift estimation, static bands

**Files:**
- Create: `include/longshot/LongshotAnalyzer.h`, `src/longshot/LongshotAnalyzer.cpp`
- Create: `tests/Longshot/tst_LongshotAnalyzer.cpp`
- Modify: `CMakeLists.txt` (`snaptray_algorithms` sources), `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `LongshotTypes.h` (Task 1), `MatConverter::toGray(const QImage&)` (`include/utils/MatConverter.h`, returns `cv::Mat` CV_8UC1 — private to `snaptray_algorithms`), `SyntheticScroll` (Task 3, tests only).
- Produces (namespace `SnapTray::Longshot`):
  - `struct AnalyzerParams { int coarseBins = 1; double minPeakScore = 0.55; double minConfidence = 0.35; int refineRadius = 4; int templateRows = 96; double staticRowDiffThreshold = 4.0; double inkGradientThreshold = 2.0; double movingColumnDiffThreshold = 6.0; int maxShiftFraction = 2; }`
  - `FrameFeatures LongshotAnalyzer::computeFeatures(const QImage& frame, qint64 tMs)` — fills `tMs`, `rowMean`, `rowGradient`, `validContentRect = frame.rect()`, bands zero, `stationary = false`.
  - `struct ShiftObservation { PairShift shift; StaticBands bandsFrom; StaticBands bandsTo; QRect movingSpanFrom; QRect movingSpanTo; bool stationary; }`
  - `std::optional<ShiftObservation> LongshotAnalyzer::estimateShift(const QImage& from, const FrameFeatures& fromFeatures, const QImage& to, const FrameFeatures& toFeatures, int fromIndex, int toIndex, const AnalyzerParams& params)`.
  - **Sign convention (binding for Tasks 5–6):** `PairShift::dy = pageOffset(to) − pageOffset(from)`; positive when the user scrolled down (content moved up on screen). A stationary pair returns `dy = 0`, `stationary = true`, confidence 1.
  - `StaticBands LongshotAnalyzer::detectStaticBands(const QImage& from, const QImage& to, int dy, const AnalyzerParams&)` and `QRect LongshotAnalyzer::movingColumnSpan(const QImage& from, const QImage& to, int dy, const AnalyzerParams&)` (crop-local rect spanning the columns whose content moved; full height).

- [ ] **Step 1: Confirm the sign convention**

`include/longshot/LongshotTypes.h` (Task 1) documents `PairShift::dy = pageOffset(to) − pageOffset(from)`, positive when the user scrolled down. Every test below is written against that convention; if a result comes out negated, the implementation is wrong, not the tests.

- [ ] **Step 2: Write the failing tests**

`tests/Longshot/tst_LongshotAnalyzer.cpp`:

```cpp
#include "SyntheticScroll.h"
#include "longshot/LongshotAnalyzer.h"

#include <QtTest>

using namespace SnapTray::Longshot;
using namespace SyntheticScroll;

namespace {

struct Pair {
    QImage from;
    QImage to;
    FrameFeatures fromFeatures;
    FrameFeatures toFeatures;
};

Pair makePair(const QImage& page, int offsetFrom, int offsetTo, const Disturbances& d = {})
{
    Trajectory t;
    t.offsets = {offsetFrom, offsetTo};
    Pair p;
    p.from = renderFrame(page, kViewport, t, 0, d);
    p.to = renderFrame(page, kViewport, t, 1, d);
    p.fromFeatures = LongshotAnalyzer::computeFeatures(p.from, 0);
    p.toFeatures = LongshotAnalyzer::computeFeatures(p.to, 50);
    return p;
}

constexpr int kShiftTolerance = 1;

} // namespace

class tst_LongshotAnalyzer : public QObject
{
    Q_OBJECT
private slots:
    void featuresHaveOneEntryPerRow();
    void recoversKnownShift_data();
    void recoversKnownShift();
    void stationaryIsDetected();
    void uniformRegionIsAmbiguous();
    void stickyHeaderBecomesTopBand();
    void tallHeaderLeavesContentOrRejects();
    void sidebarExcludedFromMovingSpan();
    void scrollUpHeaderIsPerFrame();
    void hoverChangeDoesNotBreakShift();
};

void tst_LongshotAnalyzer::featuresHaveOneEntryPerRow()
{
    const QImage page = renderPage(PageSpec{});
    const Pair p = makePair(page, 100, 100);
    QCOMPARE(p.fromFeatures.rowMean.size(), size_t(kViewport.height()));
    QCOMPARE(p.fromFeatures.rowGradient.size(), size_t(kViewport.height()));
    QCOMPARE(p.fromFeatures.validContentRect, QRect(QPoint(0, 0), kViewport));
    // A text row is darker than the white gap above it.
    QVERIFY(*std::min_element(p.fromFeatures.rowMean.begin(), p.fromFeatures.rowMean.end()) < 250.0f);
    QVERIFY(*std::max_element(p.fromFeatures.rowGradient.begin(), p.fromFeatures.rowGradient.end()) > 2.0f);
}

void tst_LongshotAnalyzer::recoversKnownShift_data()
{
    QTest::addColumn<int>("dy");
    QTest::newRow("down 7") << 7;
    QTest::newRow("down 40") << 40;
    QTest::newRow("up 33") << -33;
    QTest::newRow("down 150") << 150;
    QTest::newRow("down 230 (near half viewport)") << 230;
}

void tst_LongshotAnalyzer::recoversKnownShift()
{
    QFETCH(int, dy);
    const QImage page = renderPage(PageSpec{});
    const Pair p = makePair(page, 1000, 1000 + dy);
    const auto obs = LongshotAnalyzer::estimateShift(p.from, p.fromFeatures, p.to, p.toFeatures, 0, 1, AnalyzerParams{});
    QVERIFY(obs.has_value());
    QCOMPARE(obs->shift.from, 0);
    QCOMPARE(obs->shift.to, 1);
    QVERIFY2(qAbs(obs->shift.dy - dy) <= kShiftTolerance,
             qPrintable(QStringLiteral("dy %1 expected %2").arg(obs->shift.dy).arg(dy)));
    QVERIFY2(obs->shift.confidence >= 0.5, qPrintable(QString::number(obs->shift.confidence)));
    QVERIFY(!obs->stationary);
    QCOMPARE(obs->bandsFrom, StaticBands{});
    QCOMPARE(obs->bandsTo, StaticBands{});
}

void tst_LongshotAnalyzer::stationaryIsDetected()
{
    const QImage page = renderPage(PageSpec{});
    const Pair p = makePair(page, 700, 700);
    const auto obs = LongshotAnalyzer::estimateShift(p.from, p.fromFeatures, p.to, p.toFeatures, 3, 4, AnalyzerParams{});
    QVERIFY(obs.has_value());
    QVERIFY(obs->stationary);
    QCOMPARE(obs->shift.dy, 0);
    QCOMPARE(obs->shift.confidence, 1.0);
}

void tst_LongshotAnalyzer::uniformRegionIsAmbiguous()
{
    PageSpec spec;
    spec.blankTop = 1500;
    spec.blankHeight = 1400; // taller than two viewports
    const QImage page = renderPage(spec);
    // Both frames entirely inside the blank band: nothing to match on.
    const Pair p = makePair(page, 1600, 1650);
    const auto obs = LongshotAnalyzer::estimateShift(p.from, p.fromFeatures, p.to, p.toFeatures, 0, 1, AnalyzerParams{});
    // Two blank frames are indistinguishable from a stationary pair; without
    // ink the analyzer must refuse to call them either.
    QVERIFY(!obs.has_value());
    const Pair same = makePair(page, 1600, 1600);
    QVERIFY(!LongshotAnalyzer::estimateShift(same.from, same.fromFeatures, same.to, same.toFeatures, 0, 1, AnalyzerParams{}).has_value());
}

void tst_LongshotAnalyzer::stickyHeaderBecomesTopBand()
{
    const QImage page = renderPage(PageSpec{});
    Disturbances d;
    d.stickyHeaderHeight = 40;
    const Pair p = makePair(page, 1000, 1060, d);
    const auto obs = LongshotAnalyzer::estimateShift(p.from, p.fromFeatures, p.to, p.toFeatures, 0, 1, AnalyzerParams{});
    QVERIFY(obs.has_value());
    QVERIFY2(qAbs(obs->shift.dy - 60) <= kShiftTolerance, qPrintable(QString::number(obs->shift.dy)));
    QVERIFY2(qAbs(obs->bandsFrom.top - 40) <= 2, qPrintable(QString::number(obs->bandsFrom.top)));
    QVERIFY2(qAbs(obs->bandsTo.top - 40) <= 2, qPrintable(QString::number(obs->bandsTo.top)));
    QCOMPARE(obs->bandsTo.bottom, 0);
}

void tst_LongshotAnalyzer::tallHeaderLeavesContentOrRejects()
{
    const QImage page = renderPage(PageSpec{});
    Disturbances d;
    d.stickyHeaderHeight = 300; // more than half the 480 px viewport
    const Pair p = makePair(page, 1000, 1040, d);
    const auto obs = LongshotAnalyzer::estimateShift(p.from, p.fromFeatures, p.to, p.toFeatures, 0, 1, AnalyzerParams{});
    if (obs.has_value()) {
        QVERIFY2(qAbs(obs->shift.dy - 40) <= kShiftTolerance, qPrintable(QString::number(obs->shift.dy)));
        QVERIFY(obs->bandsTo.top >= 290);
    }
    // Either outcome is acceptable; a wrong shift is not.
}

void tst_LongshotAnalyzer::sidebarExcludedFromMovingSpan()
{
    const QImage page = renderPage(PageSpec{});
    Disturbances d;
    d.sidebarWidth = 100;
    const Pair p = makePair(page, 1000, 1050, d);
    const auto obs = LongshotAnalyzer::estimateShift(p.from, p.fromFeatures, p.to, p.toFeatures, 0, 1, AnalyzerParams{});
    QVERIFY(obs.has_value());
    QVERIFY2(qAbs(obs->shift.dy - 50) <= kShiftTolerance, qPrintable(QString::number(obs->shift.dy)));
    QVERIFY2(obs->movingSpanTo.right() <= kViewport.width() - 100 + 2, qPrintable(QString::number(obs->movingSpanTo.right())));
    QVERIFY(obs->movingSpanTo.left() <= 30);
    QCOMPARE(obs->movingSpanTo.height(), kViewport.height());
}

void tst_LongshotAnalyzer::scrollUpHeaderIsPerFrame()
{
    const QImage page = renderPage(PageSpec{});
    Disturbances d;
    d.scrollUpHeaderHeight = 48;
    // Three frames: 1200 (scrolling down, no header), 1140 and 1080 (both
    // scrolling up, both show the header). A header is only detectable where
    // both frames of a pair show it, so the down->up pair reports no band
    // while the up->up pair reports it for both of its frames. Task 5
    // resolves per frame, so frame 1140 ends up with the band and frame 1200
    // without one — the header never becomes a global crop.
    Trajectory t;
    t.offsets = {1200, 1140, 1080};
    const QImage f0 = renderFrame(page, kViewport, t, 0, d);
    const QImage f1 = renderFrame(page, kViewport, t, 1, d);
    const QImage f2 = renderFrame(page, kViewport, t, 2, d);
    const auto downUp = LongshotAnalyzer::estimateShift(f0, LongshotAnalyzer::computeFeatures(f0, 0), f1,
                                                        LongshotAnalyzer::computeFeatures(f1, 50), 0, 1, AnalyzerParams{});
    QVERIFY(downUp.has_value());
    QVERIFY2(qAbs(downUp->shift.dy + 60) <= kShiftTolerance, qPrintable(QString::number(downUp->shift.dy)));
    QCOMPARE(downUp->bandsFrom.top, 0);
    const auto upUp = LongshotAnalyzer::estimateShift(f1, LongshotAnalyzer::computeFeatures(f1, 50), f2,
                                                      LongshotAnalyzer::computeFeatures(f2, 100), 1, 2, AnalyzerParams{});
    QVERIFY(upUp.has_value());
    QVERIFY2(qAbs(upUp->shift.dy + 60) <= kShiftTolerance, qPrintable(QString::number(upUp->shift.dy)));
    QVERIFY2(upUp->bandsTo.top >= 44 && upUp->bandsTo.top <= 52, qPrintable(QString::number(upUp->bandsTo.top)));
    QCOMPARE(upUp->bandsFrom.top, upUp->bandsTo.top);
}

void tst_LongshotAnalyzer::hoverChangeDoesNotBreakShift()
{
    const QImage page = renderPage(PageSpec{});
    Disturbances d;
    d.hoverChange = true;
    Trajectory t;
    t.offsets = {880, 910, 940};
    const QImage a = renderFrame(page, kViewport, t, 0, d); // frame 0: colour A
    const QImage b = renderFrame(page, kViewport, t, 1, d); // frame 1: colour B
    const auto obs = LongshotAnalyzer::estimateShift(a, LongshotAnalyzer::computeFeatures(a, 0),
                                                     b, LongshotAnalyzer::computeFeatures(b, 50), 0, 1, AnalyzerParams{});
    QVERIFY(obs.has_value());
    QVERIFY2(qAbs(obs->shift.dy - 30) <= kShiftTolerance, qPrintable(QString::number(obs->shift.dy)));
}

QTEST_MAIN(tst_LongshotAnalyzer)
#include "tst_LongshotAnalyzer.moc"
```

`tests/CMakeLists.txt`:

```cmake
add_executable(Longshot_Analyzer Longshot/tst_LongshotAnalyzer.cpp)
target_link_libraries(Longshot_Analyzer PRIVATE Longshot_SyntheticScroll Qt6::Test)
add_test(NAME Longshot_Analyzer COMMAND Longshot_Analyzer)
set_tests_properties(Longshot_Analyzer PROPERTIES TIMEOUT 120 LABELS "unit")
```

- [ ] **Step 3: Run the tests to verify they fail**

Build `Longshot_Analyzer`. Expected: `Cannot open include file: 'longshot/LongshotAnalyzer.h'`.

- [ ] **Step 4: Write the header**

`include/longshot/LongshotAnalyzer.h`:

```cpp
#pragma once

#include "longshot/LongshotTypes.h"

#include <QImage>

#include <optional>

namespace SnapTray::Longshot {

struct AnalyzerParams {
    double minPeakScore = 0.55;          // NCC below this is not a match
    double minConfidence = 0.35;         // best + margin below this is ambiguous
    int refineRadius = 4;                // rows searched around each coarse candidate
    int templateRows = 96;               // height of the NCC template band
    double staticRowDiffThreshold = 4.0; // mean |luma diff| for a row to count as unchanged
    double inkGradientThreshold = 2.0;   // rows with less vertical gradient carry no information
    double movingColumnDiffThreshold = 6.0; // a column with less unshifted change is static
    int maxShiftFraction = 2;            // |dy| may not exceed height / maxShiftFraction
    int coarseCandidates = 3;            // phase-correlation peaks refined by NCC
};

// Result of matching one frame pair. Bands and spans are per frame and in
// crop-local coordinates; the shift keeps the crop-local origin.
struct ShiftObservation {
    PairShift shift;
    StaticBands bandsFrom;
    StaticBands bandsTo;
    QRect movingSpanFrom; // columns whose content moved; full frame height
    QRect movingSpanTo;
    bool stationary = false;
};

// Pass 1 primitives. Pure functions over crop-local RGB32 frames; OpenCV is
// an implementation detail of the .cpp.
class LongshotAnalyzer
{
public:
    static FrameFeatures computeFeatures(const QImage& frame, qint64 tMs);

    // Coarse candidates from 1-D phase correlation of row-mean profiles
    // (static bands excluded), refined by normalized cross-correlation at
    // full resolution. Confidence = best score + (best - runner-up). Returns
    // nullopt when no candidate reaches minPeakScore / minConfidence.
    static std::optional<ShiftObservation> estimateShift(const QImage& from, const FrameFeatures& fromFeatures,
                                                         const QImage& to, const FrameFeatures& toFeatures,
                                                         int fromIndex, int toIndex, const AnalyzerParams& params);

    // Rows at the top/bottom that carry ink yet did not move between the two
    // frames although the content shifted by dy: a header or footer. Only
    // meaningful for dy != 0.
    static StaticBands detectStaticBands(const QImage& from, const QImage& to, int dy, const AnalyzerParams& params);

    // Columns whose content changed between the two frames (unshifted
    // comparison), i.e. the scrolling region; static side panels fall outside.
    static QRect movingColumnSpan(const QImage& from, const QImage& to, int dy, const AnalyzerParams& params);
};

} // namespace SnapTray::Longshot
```

- [ ] **Step 5: Write the implementation**

`src/longshot/LongshotAnalyzer.cpp`:

```cpp
#include "longshot/LongshotAnalyzer.h"

#include "utils/MatConverter.h"

#include <QDebug>

#include <opencv2/core.hpp>
#include <opencv2/imgproc.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>

namespace SnapTray::Longshot {

namespace {

constexpr float kLumaMax = 255.0f;
constexpr int kPeakExclusionRows = 2;   // neighbours of a chosen peak are not a second peak
constexpr int kMinContentRows = 32;     // fewer usable rows than this: give up on the pair
constexpr int kMinTemplateRows = 24;
constexpr double kStationaryMeanDiff = 1.5; // whole-frame mean |diff| below this = no motion
constexpr int kMinMovingColumns = 32;

cv::Mat gray(const QImage& image)
{
    return MatConverter::toGray(image.format() == QImage::Format_RGB32 ? image : image.convertToFormat(QImage::Format_RGB32));
}

// Mean |a - b| per row, both the same size.
std::vector<double> rowAbsDiff(const cv::Mat& a, const cv::Mat& b)
{
    cv::Mat diff;
    cv::absdiff(a, b, diff);
    std::vector<double> rows(diff.rows);
    for (int y = 0; y < diff.rows; ++y) rows[y] = cv::mean(diff.row(y))[0];
    return rows;
}

std::vector<double> columnAbsDiff(const cv::Mat& a, const cv::Mat& b)
{
    cv::Mat diff;
    cv::absdiff(a, b, diff);
    std::vector<double> cols(diff.cols);
    for (int x = 0; x < diff.cols; ++x) cols[x] = cv::mean(diff.col(x))[0];
    return cols;
}

// 1-D phase correlation of two equal-length profiles; returns the circular
// correlation response (length n) where index k means "to = from shifted by k".
std::vector<double> phaseCorrelation(const std::vector<float>& from, const std::vector<float>& to)
{
    const int n = int(from.size());
    cv::Mat a(1, n, CV_32F);
    cv::Mat b(1, n, CV_32F);
    const double meanA = std::accumulate(from.begin(), from.end(), 0.0) / n;
    const double meanB = std::accumulate(to.begin(), to.end(), 0.0) / n;
    for (int i = 0; i < n; ++i) {
        // Hann window suppresses the wrap-around seam.
        const double w = 0.5 - 0.5 * std::cos(2.0 * CV_PI * i / (n - 1));
        a.at<float>(0, i) = float((from[i] - meanA) * w);
        b.at<float>(0, i) = float((to[i] - meanB) * w);
    }
    cv::Mat fa, fb, cross, response;
    cv::dft(a, fa, cv::DFT_COMPLEX_OUTPUT);
    cv::dft(b, fb, cv::DFT_COMPLEX_OUTPUT);
    // Correlation theorem: IDFT(F_from * conj(F_to))[k] = sum_y from[y + k] * to[y],
    // which peaks at k = dy because to[y] shows the page row from[y + dy] shows.
    cv::mulSpectrums(fa, fb, cross, 0, true);
    // Normalize magnitudes -> phase only.
    for (int i = 0; i < cross.cols; ++i) {
        cv::Vec2f& c = cross.at<cv::Vec2f>(0, i);
        const float mag = std::hypot(c[0], c[1]);
        if (mag > 1e-6f) { c[0] /= mag; c[1] /= mag; } else { c[0] = c[1] = 0.0f; }
    }
    cv::dft(cross, response, cv::DFT_INVERSE | cv::DFT_REAL_OUTPUT | cv::DFT_SCALE);
    std::vector<double> out(n);
    for (int i = 0; i < n; ++i) out[i] = response.at<float>(0, i);
    return out;
}

// Top-k local maxima of the response as signed shifts in [-n/2, n/2).
std::vector<int> topShifts(const std::vector<double>& response, int count, int maxAbsShift)
{
    const int n = int(response.size());
    std::vector<std::pair<double, int>> peaks;
    for (int k = 0; k < n; ++k) {
        const int shift = k < n / 2 ? k : k - n;
        if (std::abs(shift) > maxAbsShift) continue;
        peaks.push_back({response[k], shift});
    }
    std::sort(peaks.begin(), peaks.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
    std::vector<int> chosen;
    for (const auto& [score, shift] : peaks) {
        if (std::any_of(chosen.begin(), chosen.end(), [&](int c) { return std::abs(c - shift) <= kPeakExclusionRows; })) continue;
        chosen.push_back(shift);
        if (int(chosen.size()) == count) break;
    }
    return chosen;
}

// NCC of a template band taken from `to` (content rows only) against `from`
// shifted by candidate dy, searched within +-radius. Returns the best score
// and the refined dy. With dy = pageOffset(to) - pageOffset(from): a row y of
// `to` shows page row offsetTo + y, which in `from` is row y + dy.
std::pair<double, int> refineByNcc(const cv::Mat& from, const cv::Mat& to, int candidate, int radius,
                                   const QRect& contentTo, const QRect& contentFrom, int templateRows)
{
    // Template: centre band of `to` within its content rect and moving columns.
    const int tplHeight = std::clamp(templateRows, kMinTemplateRows, std::max(kMinTemplateRows, contentTo.height() / 2));
    const int tplTop = contentTo.top() + (contentTo.height() - tplHeight) / 2;
    const cv::Rect tplRect(contentTo.left(), tplTop, contentTo.width(), tplHeight);
    if (tplRect.width <= 0 || tplRect.height <= 0) return {0.0, candidate};
    // Search window in `from`: rows [tplTop + candidate - radius, ... + tplHeight + radius).
    const int searchTop = tplTop + candidate - radius;
    const int searchBottom = tplTop + candidate + tplHeight + radius; // exclusive
    const int clampedTop = std::max(contentFrom.top(), searchTop);
    const int clampedBottom = std::min(contentFrom.top() + contentFrom.height(), searchBottom);
    if (clampedBottom - clampedTop < tplHeight) return {0.0, candidate};
    const cv::Rect searchRect(contentTo.left(), clampedTop, contentTo.width(), clampedBottom - clampedTop);
    if (searchRect.x + searchRect.width > from.cols || searchRect.y + searchRect.height > from.rows) return {0.0, candidate};
    cv::Mat result;
    cv::matchTemplate(from(searchRect), to(tplRect), result, cv::TM_CCOEFF_NORMED);
    double minVal = 0.0, maxVal = 0.0;
    cv::Point minLoc, maxLoc;
    cv::minMaxLoc(result, &minVal, &maxVal, &minLoc, &maxLoc);
    const int matchedRowInFrom = clampedTop + maxLoc.y;
    return {std::isfinite(maxVal) ? maxVal : 0.0, matchedRowInFrom - tplTop};
}

} // namespace

FrameFeatures LongshotAnalyzer::computeFeatures(const QImage& frame, qint64 tMs)
{
    FrameFeatures f;
    f.tMs = tMs;
    const cv::Mat g = gray(frame);
    f.rowMean.resize(g.rows);
    f.rowGradient.resize(g.rows);
    for (int y = 0; y < g.rows; ++y) {
        f.rowMean[y] = float(cv::mean(g.row(y))[0]);
        if (y + 1 < g.rows) {
            cv::Mat diff;
            cv::absdiff(g.row(y + 1), g.row(y), diff);
            f.rowGradient[y] = float(cv::mean(diff)[0]);
        } else {
            f.rowGradient[y] = y > 0 ? f.rowGradient[y - 1] : 0.0f;
        }
    }
    f.validContentRect = frame.rect();
    return f;
}

StaticBands LongshotAnalyzer::detectStaticBands(const QImage& fromImage, const QImage& toImage, int dy,
                                                const AnalyzerParams& params)
{
    StaticBands bands;
    if (dy == 0) return bands;
    const cv::Mat from = gray(fromImage);
    const cv::Mat to = gray(toImage);
    if (from.size() != to.size()) return bands;
    const std::vector<double> unchanged = rowAbsDiff(from, to);
    const FrameFeatures toFeatures = computeFeatures(toImage, 0);
    // Rows that have ink and did not move, scanning in from the edges. Rows
    // without ink are skipped (they cannot tell us anything) but end the band
    // once a moving inked row has been seen.
    auto scan = [&](int start, int step) {
        int band = 0;
        int y = start;
        while (y >= 0 && y < to.rows) {
            const bool inked = toFeatures.rowGradient[y] >= params.inkGradientThreshold;
            if (inked) {
                if (unchanged[y] > params.staticRowDiffThreshold) break;
                band = step > 0 ? y + 1 : to.rows - y;
            }
            y += step;
        }
        return band;
    };
    bands.top = scan(0, 1);
    bands.bottom = scan(to.rows - 1, -1);
    // A whole-frame "band" means the frame did not move at all: that is
    // stationary, not a header, and the caller handles it before calling here.
    if (bands.top + bands.bottom >= to.rows) return StaticBands{};
    return bands;
}

QRect LongshotAnalyzer::movingColumnSpan(const QImage& fromImage, const QImage& toImage, int dy,
                                         const AnalyzerParams& params)
{
    Q_UNUSED(dy);
    const cv::Mat from = gray(fromImage);
    const cv::Mat to = gray(toImage);
    const QRect full(0, 0, to.cols, to.rows);
    if (from.size() != to.size()) return full;
    const std::vector<double> unchanged = columnAbsDiff(from, to);
    int left = 0;
    while (left < to.cols && unchanged[left] < params.movingColumnDiffThreshold) ++left;
    int right = to.cols - 1;
    while (right > left && unchanged[right] < params.movingColumnDiffThreshold) --right;
    if (right - left + 1 < kMinMovingColumns) return full;
    return QRect(left, 0, right - left + 1, to.rows);
}

std::optional<ShiftObservation> LongshotAnalyzer::estimateShift(const QImage& fromImage, const FrameFeatures& fromFeatures,
                                                                const QImage& toImage, const FrameFeatures& toFeatures,
                                                                int fromIndex, int toIndex, const AnalyzerParams& params)
{
    if (fromImage.size() != toImage.size() || fromImage.isNull()) return std::nullopt;
    const cv::Mat from = gray(fromImage);
    const cv::Mat to = gray(toImage);
    const int height = to.rows;
    const int width = to.cols;

    ShiftObservation obs;
    obs.shift.from = fromIndex;
    obs.shift.to = toIndex;
    obs.movingSpanFrom = QRect(0, 0, width, height);
    obs.movingSpanTo = obs.movingSpanFrom;

    // Frames without ink carry no information: two blank frames look
    // stationary but could be anywhere inside a blank region. Refuse them.
    int inkedAnywhere = 0;
    for (int y = 0; y < height; ++y) {
        if (fromFeatures.rowGradient[y] >= params.inkGradientThreshold || toFeatures.rowGradient[y] >= params.inkGradientThreshold) ++inkedAnywhere;
    }
    if (inkedAnywhere < kMinContentRows) {
        qDebug() << "LongshotAnalyzer: pair" << fromIndex << "->" << toIndex << "has no ink; ambiguous";
        return std::nullopt;
    }

    // Stationary pair: nothing changed anywhere.
    const std::vector<double> unchangedRows = rowAbsDiff(from, to);
    const double meanDiff = std::accumulate(unchangedRows.begin(), unchangedRows.end(), 0.0) / height;
    if (meanDiff < kStationaryMeanDiff) {
        obs.stationary = true;
        obs.shift.dy = 0;
        obs.shift.confidence = 1.0;
        return obs;
    }

    const int maxAbsShift = height / std::max(1, params.maxShiftFraction);

    // Coarse pass 1: full-profile phase correlation gives a first dy.
    std::vector<int> candidates = topShifts(phaseCorrelation(fromFeatures.rowMean, toFeatures.rowMean),
                                            params.coarseCandidates, maxAbsShift);
    if (candidates.empty()) return std::nullopt;

    // Static bands from the leading candidate, then redo the coarse pass on
    // content rows only so a header cannot pin the correlation at zero.
    StaticBands bands = detectStaticBands(fromImage, toImage, candidates.front(), params);
    const QRect span = movingColumnSpan(fromImage, toImage, candidates.front(), params);
    const int contentTop = bands.top;
    const int contentBottom = height - bands.bottom; // exclusive
    if (contentBottom - contentTop < kMinContentRows) {
        qDebug() << "LongshotAnalyzer: too little content between bands" << bands.top << bands.bottom;
        return std::nullopt;
    }
    std::vector<float> fromProfile(fromFeatures.rowMean.begin() + contentTop, fromFeatures.rowMean.begin() + contentBottom);
    std::vector<float> toProfile(toFeatures.rowMean.begin() + contentTop, toFeatures.rowMean.begin() + contentBottom);
    // Rows without ink in BOTH frames add nothing but noise; a fully blank
    // content area has no information at all.
    int inkedRows = 0;
    for (int y = contentTop; y < contentBottom; ++y) {
        if (fromFeatures.rowGradient[y] >= params.inkGradientThreshold || toFeatures.rowGradient[y] >= params.inkGradientThreshold) ++inkedRows;
    }
    if (inkedRows < kMinContentRows) {
        qDebug() << "LongshotAnalyzer: content area has no ink; ambiguous";
        return std::nullopt;
    }
    candidates = topShifts(phaseCorrelation(fromProfile, toProfile), params.coarseCandidates,
                           std::min(maxAbsShift, (contentBottom - contentTop) / std::max(1, params.maxShiftFraction)));
    if (candidates.empty()) return std::nullopt;

    // Fine pass: NCC around each candidate; best + margin = confidence.
    const QRect contentTo(span.left(), contentTop, span.width(), contentBottom - contentTop);
    const QRect contentFrom = contentTo;
    std::vector<std::pair<double, int>> refined;
    for (int candidate : candidates) {
        refined.push_back(refineByNcc(from, to, candidate, params.refineRadius, contentTo, contentFrom, params.templateRows));
    }
    std::sort(refined.begin(), refined.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
    const double best = refined.front().first;
    double runnerUp = 0.0;
    for (size_t i = 1; i < refined.size(); ++i) {
        if (std::abs(refined[i].second - refined.front().second) > kPeakExclusionRows) { runnerUp = refined[i].first; break; }
    }
    const double confidence = std::clamp(best + (best - runnerUp), 0.0, 1.0);
    if (best < params.minPeakScore || confidence < params.minConfidence) {
        qDebug() << "LongshotAnalyzer: ambiguous pair" << fromIndex << "->" << toIndex << "best" << best
                 << "runner-up" << runnerUp;
        return std::nullopt;
    }
    obs.shift.dy = refined.front().second;
    obs.shift.confidence = confidence;
    // Static bands are rows with ink that stayed put while the content moved.
    // Such rows exist in both frames of the pair by construction (an overlay
    // present in only one frame is indistinguishable from newly revealed
    // content within a single pair), so the band is attributed to both; the
    // pipeline resolves per frame across all of a frame's pairs.
    const StaticBands finalBands = detectStaticBands(fromImage, toImage, obs.shift.dy, params);
    obs.bandsTo = finalBands;
    obs.bandsFrom = finalBands;
    obs.movingSpanFrom = span;
    obs.movingSpanTo = span;
    return obs;
}

} // namespace SnapTray::Longshot
```

Add `src/longshot/LongshotAnalyzer.cpp` to the `snaptray_algorithms` source list.

- [ ] **Step 6: Run the tests to verify they pass**

Build and run `Longshot_Analyzer` (`-o <scratch>/t4.txt,txt`). Expected: 10 slots pass. Debugging guide, in order: if every `recoversKnownShift` row returns `-dy`, the sign is inverted in `refineByNcc`'s search placement (`tplTop + candidate`) or in the `mulSpectrums` argument order — flip once, consistently, so that the test's convention (`dy = pageOffset(to) − pageOffset(from)`) holds; if `scrollUpHeaderIsPerFrame`'s down→up pair reports a band, `detectStaticBands` is counting rows that differ (header vs content) as unchanged — check the threshold direction; if `uniformRegionIsAmbiguous` returns a value, the ink check runs after the stationary check instead of before it. Do not loosen test tolerances.

- [ ] **Step 7: Commit**

```bash
git add include/longshot/LongshotTypes.h include/longshot/LongshotAnalyzer.h src/longshot/LongshotAnalyzer.cpp tests/Longshot/tst_LongshotAnalyzer.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(longshot): estimate pair shifts with static-band masks"
```

---
### Task 5: LongshotPipeline — pass 1 driver, loop closures, global solve

**Files:**
- Create: `include/longshot/LongshotPipeline.h`, `src/longshot/LongshotPipeline.cpp`
- Create: `tests/Longshot/tst_LongshotPipeline.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `LongshotFrameSource` (Task 2), `LongshotAnalyzer` (Task 4), `PositionSolver` (Task 1), `SyntheticScroll` (tests).
- Produces (namespace `SnapTray::Longshot`):
  - `struct AnalysisResult { LongshotError error = LongshotError::None; QSize frameSize; double frameRate = 0.0; std::vector<FrameFeatures> frames; std::vector<QImage> thumbnails; /* gray, kThumbnailWidth wide, one per frame */ std::vector<PairShift> edges; /* chain + closures, after rejection */ SolveResult solve; int rejectedPairs = 0; int closuresTried = 0; int closuresAccepted = 0; int islandsRejoined = 0; }`
  - `using ProgressFn = std::function<bool(int percent)>;` (return false to cancel)
  - `struct PipelineParams { AnalyzerParams analyzer; double maxResidualPx = 3.0; int loopMinGapFrames = 5; int maxClosureFrames = 64; qint64 closureFrameBudgetBytes = 256 * 1024 * 1024; int thumbnailWidth = 160; }`
  - `AnalysisResult LongshotPipeline::analyze(LongshotFrameSource& source, const PipelineParams& params, const ProgressFn& progress)` — `source` must already be `open()`ed; the pipeline may call `open()` again with the same arguments for its second decode pass, so it also takes the open arguments: full signature `analyze(LongshotFrameSource& source, const QString& path, qint64 startMs, qint64 endMs, const QRect& crop, const PipelineParams&, const ProgressFn&)`.
  - Pure helpers (public, for tests): `static std::vector<std::pair<int,int>> LongshotPipeline::selectClosureCandidates(const SolveResult&, int frameHeight, const std::vector<qint64>& frameTimes, int loopMinGapFrames, int maxFrames)`; `static void LongshotPipeline::resolveFrameMasks(std::vector<FrameFeatures>&, const std::vector<ShiftObservation>&)`.
  - Task 6 renders from `AnalysisResult`; Task 7 caches it.

- [ ] **Step 1: Write the failing tests**

`tests/Longshot/tst_LongshotPipeline.cpp`:

```cpp
#include "SyntheticScroll.h"
#include "longshot/FrameReaderLongshotSource.h"
#include "longshot/LongshotPipeline.h"

#include <QtTest>
#include <QTemporaryDir>

#include <set>

using namespace SnapTray::Longshot;
using namespace SyntheticScroll;

namespace {

constexpr int kPositionTolerance = 2;
constexpr double kMinPlacedFraction = 0.95;

struct Recording {
    QString path;
    Trajectory trajectory;
    QImage page;
};

// Renders, encodes and returns the recording; empty path on failure (message in error).
Recording record(const QString& path, const PageSpec& spec, const Trajectory& trajectory, const Disturbances& d, QString* error)
{
    Recording r;
    r.page = renderPage(spec);
    r.trajectory = clampTrajectory(trajectory, r.page.height(), kViewport.height());
    std::vector<QImage> frames;
    for (size_t i = 0; i < r.trajectory.offsets.size(); ++i) frames.push_back(renderFrame(r.page, kViewport, r.trajectory, int(i), d));
    *error = encodeFrames(path, frames, kFrameRate);
    if (error->isEmpty()) r.path = path;
    return r;
}

// Fraction of placed frames whose solved position matches the trajectory
// (relative to the first placed frame) within tolerance.
double placedAccuracy(const AnalysisResult& a, const Trajectory& t, int* placedCount)
{
    int placed = 0;
    int accurate = 0;
    int anchorIndex = -1;
    for (size_t i = 0; i < a.solve.positions.size(); ++i) {
        if (!a.solve.positions[i].has_value()) continue;
        if (anchorIndex < 0) anchorIndex = int(i);
        ++placed;
        const int expected = t.offsets[i] - t.offsets[anchorIndex];
        if (qAbs(*a.solve.positions[i] - expected) <= kPositionTolerance) ++accurate;
    }
    *placedCount = placed;
    return placed ? double(accurate) / placed : 0.0;
}

} // namespace

class tst_LongshotPipeline : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void closureCandidatesPreferOverlapAndGap();
    void masksResolvePerFrame();
    void trajectories_data();
    void trajectories();
    void stationaryRecordingYieldsOneFrame();
    void blankGapReportsBreak();
    void tooFewFramesFails();
    void cancelStopsEarly();

private:
    QTemporaryDir m_dir;
};

void tst_LongshotPipeline::initTestCase()
{
    QVERIFY(m_dir.isValid());
    if (!FrameReaderLongshotSource::createNative()) QSKIP("No frame reader on this platform");
}

void tst_LongshotPipeline::closureCandidatesPreferOverlapAndGap()
{
    SolveResult solve;
    // Frames 0..9 scrolled down 100 each, then 10..19 back up 100 each: frame 2 and frame 18 overlap.
    std::vector<qint64> times;
    for (int i = 0; i < 20; ++i) {
        solve.positions.push_back(i < 10 ? i * 100 : (9 - (i - 10)) * 100);
        times.push_back(i * 50);
    }
    const auto pairs = LongshotPipeline::selectClosureCandidates(solve, 480, times, 5, 64);
    QVERIFY(!pairs.empty());
    bool crossesLegs = false;
    for (const auto& [a, b] : pairs) {
        QVERIFY(b - a >= 5);                                   // not chain neighbours
        QVERIFY(qAbs(*solve.positions[a] - *solve.positions[b]) < 480); // overlapping viewports
        if (a < 10 && b >= 10) crossesLegs = true;             // a down-leg frame against an up-leg frame
    }
    QVERIFY(crossesLegs);
    // Unplaced frames (another island) pair with nearby placed ones so islands
    // can be rejoined; the time-gap rule does not apply to rejoin pairs, only
    // the already-tried chain neighbours (gap 1) are skipped.
    solve.positions[15] = std::nullopt;
    const auto withIsland = LongshotPipeline::selectClosureCandidates(solve, 480, times, 5, 64);
    int rejoinPairs = 0;
    for (const auto& [a, b] : withIsland) {
        if (a == 15 || b == 15) { ++rejoinPairs; QVERIFY(b - a >= 2); }
    }
    QVERIFY(rejoinPairs >= 2);
    // The frame budget bounds how many distinct frames are involved.
    const auto tight = LongshotPipeline::selectClosureCandidates(solve, 480, times, 5, 4);
    std::set<int> distinct;
    for (const auto& [a, b] : tight) { distinct.insert(a); distinct.insert(b); }
    QVERIFY(distinct.size() <= 4);
}

void tst_LongshotPipeline::masksResolvePerFrame()
{
    std::vector<FrameFeatures> frames(3);
    for (auto& f : frames) f.validContentRect = QRect(0, 0, 640, 480);
    ShiftObservation a;
    a.shift = {0, 1, 60, 0.9};
    a.bandsFrom = {40, 0, 0, 0};
    a.bandsTo = {40, 0, 0, 0};
    a.movingSpanFrom = a.movingSpanTo = QRect(0, 0, 540, 480);
    ShiftObservation b;
    b.shift = {1, 2, -60, 0.9};
    b.bandsFrom = {0, 0, 0, 0};
    b.bandsTo = {48, 0, 0, 0}; // scroll-up header on frame 2 only
    b.movingSpanFrom = b.movingSpanTo = QRect(0, 0, 540, 480);
    LongshotPipeline::resolveFrameMasks(frames, {a, b});
    QCOMPARE(frames[0].excludedBands.top, 40);
    QCOMPARE(frames[1].excludedBands.top, 40); // max over its observations
    QCOMPARE(frames[2].excludedBands.top, 48);
    QCOMPARE(frames[2].validContentRect, QRect(0, 48, 540, 432));
    QCOMPARE(frames[0].validContentRect, QRect(0, 40, 540, 440));
}

void tst_LongshotPipeline::trajectories_data()
{
    QTest::addColumn<int>("kind");
    QTest::addColumn<int>("disturbance");
    QTest::newRow("constant") << 0 << 0;
    QTest::newRow("fling") << 1 << 0;
    QTest::newRow("back-and-forth") << 2 << 0;
    QTest::newRow("pauses") << 3 << 0;
    QTest::newRow("constant+sticky header") << 0 << 1;
    QTest::newRow("back-and-forth+scroll-up header") << 2 << 2;
    QTest::newRow("constant+sidebar") << 0 << 3;
    QTest::newRow("constant+hover") << 0 << 4;
    QTest::newRow("constant+lazy load") << 0 << 5;
}

void tst_LongshotPipeline::trajectories()
{
    QFETCH(int, kind);
    QFETCH(int, disturbance);
    Trajectory t;
    switch (kind) {
    case 0: t = constantSpeed(60, 0, 45); break;
    case 1: t = fling(50, 200, 140); break;
    case 2: t = backAndForth(70, 900, 600, 50); break;
    default: t = withPauses(constantSpeed(40, 0, 45), 5, 3); break;
    }
    Disturbances d;
    switch (disturbance) {
    case 1: d.stickyHeaderHeight = 40; break;
    case 2: d.scrollUpHeaderHeight = 48; break;
    case 3: d.sidebarWidth = 100; break;
    case 4: d.hoverChange = true; break;
    case 5: d.lazyLoadRow = 1400; d.lazyLoadFrame = 20; break;
    default: break;
    }
    QString error;
    const Recording r = record(m_dir.filePath(QStringLiteral("traj-%1-%2.mp4").arg(kind).arg(disturbance)), PageSpec{}, t, d, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source->open(r.path, 0, -1, QRect()));
    int lastPercent = -1;
    const AnalysisResult a = LongshotPipeline::analyze(*source, r.path, 0, -1, QRect(), PipelineParams{},
                                                      [&lastPercent](int p) { QVERIFY(p >= lastPercent); lastPercent = p; return true; });
    QCOMPARE(int(a.error), int(LongshotError::None));
    QCOMPARE(a.frames.size(), r.trajectory.offsets.size());
    QCOMPARE(a.thumbnails.size(), a.frames.size());
    QCOMPARE(lastPercent, 100);
    int placed = 0;
    const double accuracy = placedAccuracy(a, r.trajectory, &placed);
    QVERIFY2(placed >= int(kMinPlacedFraction * a.frames.size()),
             qPrintable(QStringLiteral("placed %1 of %2, breaks %3").arg(placed).arg(a.frames.size()).arg(a.solve.breakTimesMs.size())));
    QVERIFY2(accuracy >= kMinPlacedFraction, qPrintable(QStringLiteral("accuracy %1").arg(accuracy)));
    QVERIFY2(a.solve.breakTimesMs.empty(), qPrintable(QStringLiteral("breaks: %1").arg(a.solve.breakTimesMs.size())));
    if (kind == 2) QVERIFY(a.closuresAccepted > 0); // back-and-forth must produce loop closures
    if (disturbance == 1) {
        for (const FrameFeatures& f : a.frames) QVERIFY2(qAbs(f.excludedBands.top - 40) <= 2, qPrintable(QString::number(f.excludedBands.top)));
    }
    if (disturbance == 3) {
        for (const FrameFeatures& f : a.frames) QVERIFY(f.validContentRect.right() <= kViewport.width() - 100 + 2);
    }
}

void tst_LongshotPipeline::stationaryRecordingYieldsOneFrame()
{
    QString error;
    const Recording r = record(m_dir.filePath(QStringLiteral("still.mp4")), PageSpec{}, constantSpeed(30, 500, 0), Disturbances{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source->open(r.path, 0, -1, QRect()));
    const AnalysisResult a = LongshotPipeline::analyze(*source, r.path, 0, -1, QRect(), PipelineParams{}, {});
    QCOMPARE(int(a.error), int(LongshotError::None));
    QVERIFY(a.solve.breakTimesMs.empty());
    for (size_t i = 0; i < a.frames.size(); ++i) {
        QVERIFY(a.solve.positions[i].has_value());
        QCOMPARE(*a.solve.positions[i], 0);
        if (i > 0) QVERIFY(a.frames[i].stationary);
    }
}

void tst_LongshotPipeline::blankGapReportsBreak()
{
    PageSpec spec;
    spec.height = 6000;
    spec.blankTop = 2000;
    spec.blankHeight = 1400;
    QString error;
    const Recording r = record(m_dir.filePath(QStringLiteral("gap.mp4")), spec, constantSpeed(90, 800, 50), Disturbances{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source->open(r.path, 0, -1, QRect()));
    const AnalysisResult a = LongshotPipeline::analyze(*source, r.path, 0, -1, QRect(), PipelineParams{}, {});
    QCOMPARE(int(a.error), int(LongshotError::None));
    // The viewport is entirely blank for several frames: those pairs are
    // rejected and the two sides cannot be joined with confidence.
    QVERIFY2(!a.solve.breakTimesMs.empty(), "a blank gap taller than the viewport must be reported as a break");
    QVERIFY(a.rejectedPairs > 0);
    int placed = 0;
    QVERIFY(placedAccuracy(a, r.trajectory, &placed) >= kMinPlacedFraction);
    QVERIFY(placed >= 20); // the larger side survives
}

void tst_LongshotPipeline::tooFewFramesFails()
{
    QString error;
    const Recording r = record(m_dir.filePath(QStringLiteral("short.mp4")), PageSpec{}, constantSpeed(10, 0, 40), Disturbances{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source->open(r.path, 0, 40, QRect())); // 40 ms at 20 fps = one frame
    const AnalysisResult a = LongshotPipeline::analyze(*source, r.path, 0, 40, QRect(), PipelineParams{}, {});
    QCOMPARE(int(a.error), int(LongshotError::TooFewFrames));
}

void tst_LongshotPipeline::cancelStopsEarly()
{
    QString error;
    const Recording r = record(m_dir.filePath(QStringLiteral("cancel.mp4")), PageSpec{}, constantSpeed(60, 0, 45), Disturbances{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source->open(r.path, 0, -1, QRect()));
    int calls = 0;
    const AnalysisResult a = LongshotPipeline::analyze(*source, r.path, 0, -1, QRect(), PipelineParams{},
                                                      [&calls](int) { return ++calls < 3; });
    QCOMPARE(int(a.error), int(LongshotError::Cancelled));
    QVERIFY(a.frames.size() < 60);
}

QTEST_MAIN(tst_LongshotPipeline)
#include "tst_LongshotPipeline.moc"
```

`tests/CMakeLists.txt`:

```cmake
add_executable(Longshot_Pipeline Longshot/tst_LongshotPipeline.cpp)
target_link_libraries(Longshot_Pipeline PRIVATE Longshot_SyntheticScroll Qt6::Test
    "$<$<PLATFORM_ID:Windows>:mfreadwrite>" "$<$<PLATFORM_ID:Windows>:mfplat>"
    "$<$<PLATFORM_ID:Windows>:mfuuid>" "$<$<PLATFORM_ID:Windows>:ole32>")
add_test(NAME Longshot_Pipeline COMMAND Longshot_Pipeline)
set_tests_properties(Longshot_Pipeline PROPERTIES TIMEOUT 600 LABELS "integration;slow")
```

- [ ] **Step 2: Run the tests to verify they fail**

Build `Longshot_Pipeline`. Expected: `Cannot open include file: 'longshot/LongshotPipeline.h'`.

- [ ] **Step 3: Write the header**

`include/longshot/LongshotPipeline.h`:

```cpp
#pragma once

#include "longshot/LongshotAnalyzer.h"
#include "longshot/LongshotFrameSource.h"
#include "longshot/LongshotTypes.h"

#include <QImage>
#include <QString>

#include <functional>
#include <utility>
#include <vector>

namespace SnapTray::Longshot {

using ProgressFn = std::function<bool(int percent)>; // false = cancel

struct PipelineParams {
    AnalyzerParams analyzer;
    double maxResidualPx = 3.0;        // solver outlier cap
    int loopMinGapFrames = 5;          // closures only between frames this far apart in time
    int maxClosureFrames = 64;         // full-resolution frames retained for closure refinement
    qint64 closureFrameBudgetBytes = qint64(256) * 1024 * 1024; // hard memory budget for those frames
    int thumbnailWidth = 160;          // gray thumbnails kept for every frame (renderer majority check)
};

struct AnalysisResult {
    LongshotError error = LongshotError::None;
    QSize frameSize;
    double frameRate = 0.0;
    std::vector<FrameFeatures> frames;
    std::vector<QImage> thumbnails;    // Format_Grayscale8, thumbnailWidth wide, one per frame
    std::vector<PairShift> edges;      // chain + accepted closures (before solver rejection)
    SolveResult solve;
    int rejectedPairs = 0;             // chain pairs the analyzer could not match
    int closuresTried = 0;
    int closuresAccepted = 0;
    int islandsRejoined = 0;
};

// Pass 1 + global solve. Decodes the range twice at most: once sequentially
// for features and chain shifts (only the previous frame is kept), once more
// for the full-resolution frames needed by loop closures (bounded by
// maxClosureFrames and closureFrameBudgetBytes).
class LongshotPipeline
{
public:
    static AnalysisResult analyze(LongshotFrameSource& source, const QString& path, qint64 startMs, qint64 endMs,
                                  const QRect& crop, const PipelineParams& params, const ProgressFn& progress);

    // Same as analyze(), but frames whose media time appears in knownFrames
    // reuse those features and thumbnails instead of recomputing them (the
    // frame is still decoded, because the chain shift to its neighbour needs
    // the pixels). Bands and stationary flags are always re-resolved from the
    // new observations. *framesAnalyzed (optional) counts frames NOT found in
    // knownFrames. Used by the session's trim-change path.
    static AnalysisResult analyzeIncremental(LongshotFrameSource& source, const QString& path, qint64 startMs, qint64 endMs,
                                             const QRect& crop, const PipelineParams& params, const ProgressFn& progress,
                                             const std::vector<FrameFeatures>& knownFrames,
                                             const std::vector<QImage>& knownThumbnails, int* framesAnalyzed);

    // Frame pairs worth a loop-closure check: far apart in time, overlapping
    // in solved position, plus every unplaced frame against the placed frames
    // nearest in time (island rejoin). Bounded by maxFrames distinct frames.
    static std::vector<std::pair<int, int>> selectClosureCandidates(const SolveResult& solve, int frameHeight,
                                                                    const std::vector<qint64>& frameTimesMs,
                                                                    int loopMinGapFrames, int maxFrames);

    // Per-frame masks from all observations touching the frame: bands are the
    // maximum over observations; validContentRect = moving span minus bands.
    static void resolveFrameMasks(std::vector<FrameFeatures>& frames, const std::vector<ShiftObservation>& observations);
};

} // namespace SnapTray::Longshot
```

- [ ] **Step 4: Write the implementation**

`src/longshot/LongshotPipeline.cpp`:

```cpp
#include "longshot/LongshotPipeline.h"

#include "longshot/PositionSolver.h"

#include <QDebug>

#include <algorithm>
#include <map>
#include <set>

namespace SnapTray::Longshot {

namespace {

constexpr int kProgressAnalyzeEnd = 70;   // percent at the end of the first decode pass
constexpr int kProgressClosureEnd = 95;
constexpr int kBytesPerPixel = 4;
constexpr int kRejoinNeighbours = 3;      // placed frames tried on each side of an unplaced frame
constexpr int kClosureStride = 4;         // frames skipped between overlap closure checks

QImage grayThumbnail(const QImage& frame, int width)
{
    return frame.scaledToWidth(std::max(1, width), Qt::SmoothTransformation).convertToFormat(QImage::Format_Grayscale8);
}

bool report(const ProgressFn& progress, int percent)
{
    return !progress || progress(percent);
}

} // namespace

void LongshotPipeline::resolveFrameMasks(std::vector<FrameFeatures>& frames, const std::vector<ShiftObservation>& observations)
{
    std::vector<QRect> spans(frames.size());
    for (size_t i = 0; i < frames.size(); ++i) {
        frames[i].excludedBands = StaticBands{};
        spans[i] = frames[i].validContentRect;
    }
    for (const ShiftObservation& o : observations) {
        auto apply = [&](int index, const StaticBands& bands, const QRect& span) {
            if (index < 0 || index >= int(frames.size())) return;
            StaticBands& b = frames[index].excludedBands;
            b.top = std::max(b.top, bands.top);
            b.bottom = std::max(b.bottom, bands.bottom);
            b.left = std::max(b.left, bands.left);
            b.right = std::max(b.right, bands.right);
            if (!span.isEmpty()) spans[index] = spans[index].intersected(span);
        };
        apply(o.shift.from, o.bandsFrom, o.movingSpanFrom);
        apply(o.shift.to, o.bandsTo, o.movingSpanTo);
    }
    for (size_t i = 0; i < frames.size(); ++i) {
        const StaticBands& b = frames[i].excludedBands;
        QRect r = spans[i];
        r.setTop(std::max(r.top(), b.top));
        r.setBottom(std::min(r.bottom(), frames[i].validContentRect.bottom() - b.bottom));
        r.setLeft(std::max(r.left(), b.left));
        r.setRight(std::min(r.right(), frames[i].validContentRect.right() - b.right));
        frames[i].validContentRect = r.isValid() ? r : QRect();
    }
}

std::vector<std::pair<int, int>> LongshotPipeline::selectClosureCandidates(const SolveResult& solve, int frameHeight,
                                                                           const std::vector<qint64>& frameTimesMs,
                                                                           int loopMinGapFrames, int maxFrames)
{
    Q_UNUSED(frameTimesMs);
    std::vector<std::pair<int, int>> pairs;
    std::set<int> framesUsed;
    const int n = int(solve.positions.size());
    constexpr int kRejoinMinGap = 2; // chain neighbours (gap 1) were already tried and failed
    auto tryAdd = [&](int a, int b, int minGap) {
        if (a == b || a < 0 || b < 0 || a >= n || b >= n) return;
        if (a > b) std::swap(a, b);
        if (b - a < minGap) return;
        if (std::find(pairs.begin(), pairs.end(), std::make_pair(a, b)) != pairs.end()) return;
        std::set<int> next = framesUsed;
        next.insert(a);
        next.insert(b);
        if (int(next.size()) > maxFrames) return;
        framesUsed = next;
        pairs.push_back({a, b});
    };
    // Island rejoin first: each unplaced frame against the nearest placed frames in time.
    std::vector<int> placed;
    for (int i = 0; i < n; ++i) {
        if (solve.positions[i].has_value()) placed.push_back(i);
    }
    for (int i = 0; i < n; ++i) {
        if (solve.positions[i].has_value()) continue;
        int tried = 0;
        for (int p : placed) {
            if (p < i) continue;
            tryAdd(i, p, kRejoinMinGap);
            if (++tried >= kRejoinNeighbours) break;
        }
        tried = 0;
        for (auto it = placed.rbegin(); it != placed.rend(); ++it) {
            if (*it > i) continue;
            tryAdd(*it, i, kRejoinMinGap);
            if (++tried >= kRejoinNeighbours) break;
        }
    }
    // Overlap closures: placed frames far apart in time whose viewports overlap.
    for (size_t ai = 0; ai < placed.size(); ai += kClosureStride) {
        const int a = placed[ai];
        for (size_t bi = ai + 1; bi < placed.size(); ++bi) {
            const int b = placed[bi];
            if (b - a < loopMinGapFrames) continue;
            if (std::abs(*solve.positions[a] - *solve.positions[b]) >= frameHeight) continue;
            tryAdd(a, b, loopMinGapFrames);
            break; // one closure per anchor keeps the budget for other anchors
        }
    }
    return pairs;
}

AnalysisResult LongshotPipeline::analyze(LongshotFrameSource& source, const QString& path, qint64 startMs, qint64 endMs,
                                         const QRect& crop, const PipelineParams& params, const ProgressFn& progress)
{
    return analyzeIncremental(source, path, startMs, endMs, crop, params, progress, {}, {}, nullptr);
}

AnalysisResult LongshotPipeline::analyzeIncremental(LongshotFrameSource& source, const QString& path, qint64 startMs, qint64 endMs,
                                                    const QRect& crop, const PipelineParams& params, const ProgressFn& progress,
                                                    const std::vector<FrameFeatures>& knownFrames,
                                                    const std::vector<QImage>& knownThumbnails, int* framesAnalyzed)
{
    std::map<qint64, size_t> knownByTime;
    if (knownFrames.size() == knownThumbnails.size()) {
        for (size_t i = 0; i < knownFrames.size(); ++i) knownByTime[knownFrames[i].tMs] = i;
    }
    int analyzed = 0;
    AnalysisResult result;
    result.frameSize = source.frameSize();
    result.frameRate = source.frameRate();
    if (result.frameSize.width() < kMinAnalysisSide || result.frameSize.height() < kMinAnalysisSide) {
        result.error = LongshotError::CropTooSmall;
        return result;
    }
    const int expected = std::max(1, source.expectedFrameCount());

    // Pass 1: sequential features + chain shifts. Only the previous frame stays in memory.
    std::vector<ShiftObservation> observations;
    QImage previous;
    qint64 tMs = 0;
    while (auto frame = source.next(&tMs)) {
        FrameFeatures features;
        const auto known = knownByTime.find(tMs);
        if (known != knownByTime.end()) {
            features = knownFrames[known->second];
            features.excludedBands = StaticBands{};
            features.stationary = false;
            result.thumbnails.push_back(knownThumbnails[known->second]);
        } else {
            features = LongshotAnalyzer::computeFeatures(*frame, tMs);
            result.thumbnails.push_back(grayThumbnail(*frame, params.thumbnailWidth));
            ++analyzed;
        }
        const int index = int(result.frames.size());
        if (!previous.isNull()) {
            const auto obs = LongshotAnalyzer::estimateShift(previous, result.frames.back(), *frame, features, index - 1, index, params.analyzer);
            if (obs) {
                features.stationary = obs->stationary;
                observations.push_back(*obs);
                result.edges.push_back(obs->shift);
            } else {
                ++result.rejectedPairs;
            }
        }
        result.frames.push_back(std::move(features));
        previous = *frame;
        if (!report(progress, std::min(kProgressAnalyzeEnd, int(qint64(result.frames.size()) * kProgressAnalyzeEnd / expected)))) {
            result.error = LongshotError::Cancelled;
            return result;
        }
    }
    if (!source.lastError().isEmpty() && result.frames.empty()) {
        result.error = LongshotError::SourceUnavailable;
        return result;
    }
    if (result.frames.size() < 2) {
        result.error = LongshotError::TooFewFrames;
        return result;
    }
    std::vector<qint64> times;
    for (const FrameFeatures& f : result.frames) times.push_back(f.tMs);

    // First solve on chain edges only.
    result.solve = PositionSolver::solve(times, result.edges, params.maxResidualPx);

    // Loop closures and island rejoin need full-resolution frames: pick the
    // pairs, then decode the range once more keeping only those frames,
    // within the memory budget.
    const int height = result.frameSize.height();
    const qint64 bytesPerFrame = qint64(result.frameSize.width()) * height * kBytesPerPixel;
    const int budgetFrames = int(std::min<qint64>(params.maxClosureFrames, std::max<qint64>(2, params.closureFrameBudgetBytes / std::max<qint64>(1, bytesPerFrame))));
    const auto candidates = selectClosureCandidates(result.solve, height, times, params.loopMinGapFrames, budgetFrames);
    const int islandsBefore = int(result.solve.breakTimesMs.size());
    if (!candidates.empty()) {
        std::set<int> wanted;
        for (const auto& [a, b] : candidates) { wanted.insert(a); wanted.insert(b); }
        std::map<int, QImage> kept;
        if (source.open(path, startMs, endMs, crop)) {
            int index = 0;
            while (auto frame = source.next(&tMs)) {
                if (wanted.count(index)) kept[index] = *frame;
                ++index;
                if (!report(progress, kProgressAnalyzeEnd + int(qint64(index) * (kProgressClosureEnd - kProgressAnalyzeEnd) / (2 * expected)))) {
                    result.error = LongshotError::Cancelled;
                    return result;
                }
            }
        } else {
            qWarning() << "LongshotPipeline: could not reopen source for loop closures:" << source.lastError();
        }
        std::vector<ShiftObservation> closureObservations;
        for (size_t k = 0; k < candidates.size(); ++k) {
            const auto [a, b] = candidates[k];
            const auto fa = kept.find(a);
            const auto fb = kept.find(b);
            if (fa == kept.end() || fb == kept.end()) continue;
            ++result.closuresTried;
            const auto obs = LongshotAnalyzer::estimateShift(fa->second, result.frames[a], fb->second, result.frames[b], a, b, params.analyzer);
            if (obs && !obs->stationary) {
                closureObservations.push_back(*obs);
                result.edges.push_back(obs->shift);
                ++result.closuresAccepted;
            } else if (obs && obs->stationary) {
                // Same view twice at different times: a zero-shift closure is still an edge.
                closureObservations.push_back(*obs);
                result.edges.push_back(obs->shift);
                ++result.closuresAccepted;
            }
            if (!report(progress, kProgressAnalyzeEnd + (kProgressClosureEnd - kProgressAnalyzeEnd) / 2
                                      + int(qint64(k + 1) * (kProgressClosureEnd - kProgressAnalyzeEnd) / (2 * candidates.size())))) {
                result.error = LongshotError::Cancelled;
                return result;
            }
        }
        observations.insert(observations.end(), closureObservations.begin(), closureObservations.end());
        result.solve = PositionSolver::solve(times, result.edges, params.maxResidualPx);
    }
    result.islandsRejoined = std::max(0, islandsBefore - int(result.solve.breakTimesMs.size()));

    for (FrameFeatures& f : result.frames) f.validContentRect = QRect(QPoint(0, 0), result.frameSize);
    resolveFrameMasks(result.frames, observations);

    bool anyPlaced = false;
    for (const auto& p : result.solve.positions) anyPlaced = anyPlaced || p.has_value();
    if (!anyPlaced) result.error = LongshotError::NoReliableContent;
    if (framesAnalyzed) *framesAnalyzed = analyzed;
    if (!report(progress, 100)) result.error = LongshotError::Cancelled;
    qDebug() << "LongshotPipeline: frames" << result.frames.size() << "rejected pairs" << result.rejectedPairs
             << "closures" << result.closuresAccepted << "/" << result.closuresTried << "breaks" << result.solve.breakTimesMs.size()
             << "rejoined" << result.islandsRejoined;
    return result;
}

} // namespace SnapTray::Longshot
```

Add `src/longshot/LongshotPipeline.cpp` to the `snaptray_algorithms` source list.

- [ ] **Step 5: Run the tests to verify they pass**

Build and run `Longshot_Pipeline` (`-o <scratch>/t5.txt,txt`; this target encodes nine recordings and takes several minutes). Expected: all slots pass. If `trajectories(fling)` loses frames at the fastest shifts (140 px of a 480 px viewport is within `maxShiftFraction = 2`), inspect `rejectedPairs` before changing thresholds; if `blankGapReportsBreak` reports no break, the analyzer matched blank-on-blank — fix the ink check in Task 4, not the test. Record every measured accuracy in the report.

- [ ] **Step 6: Commit**

```bash
git add include/longshot/LongshotPipeline.h src/longshot/LongshotPipeline.cpp tests/Longshot/tst_LongshotPipeline.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(longshot): analyse recordings into globally solved frame positions"
```

---
### Task 6: LongshotRenderer — tiles, seams, side crop, sticky header, height cap

**Files:**
- Create: `include/longshot/LongshotRenderer.h`, `src/longshot/LongshotRenderer.cpp`
- Create: `tests/Longshot/tst_LongshotRenderer.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `AnalysisResult` (Task 5), `LongshotFrameSource` (Task 2), `LongshotOptions` (Task 1), `SyntheticScroll::compareWithGroundTruth` (tests).
- Produces (namespace `SnapTray::Longshot`):
  - `struct RenderResult { LongshotError error = LongshotError::None; QList<QImage> parts; /* one unless splitOversize */ int fullHeightPx = 0; bool heightCapped = false; int autoCroppedLeft = 0; int autoCroppedRight = 0; std::vector<int> breakRows; /* output rows where coverage was missing */ std::vector<int> lowConfidenceRows; /* output rows whose chosen frame joined via an edge with confidence < kLowConfidence */ bool stickyHeaderIncluded = false; }`
  - `RenderResult LongshotRenderer::render(LongshotFrameSource& source, const QString& path, qint64 startMs, qint64 endMs, const QRect& crop, const AnalysisResult& analysis, const LongshotOptions& options, const ProgressFn& progress)`.
  - Pure helpers (public, for tests): `struct TileAssignment { int outputTop; int outputBottom; /* exclusive */ int frameIndex; /* -1 = no coverage */ }`, `static std::vector<TileAssignment> LongshotRenderer::assignTiles(const AnalysisResult&, const LongshotOptions&, int outputHeight, int minPosition)`, `static void LongshotRenderer::placeSeams(std::vector<TileAssignment>&, const AnalysisResult&, int minPosition)`.

- [ ] **Step 1: Write the failing tests**

`tests/Longshot/tst_LongshotRenderer.cpp`:

```cpp
#include "SyntheticScroll.h"
#include "longshot/FrameReaderLongshotSource.h"
#include "longshot/LongshotPipeline.h"
#include "longshot/LongshotRenderer.h"

#include <QtTest>
#include <QTemporaryDir>

using namespace SnapTray::Longshot;
using namespace SyntheticScroll;

namespace {

constexpr double kMaxDuplicatedFraction = 0.01;
constexpr double kMaxMissingFraction = 0.01;
constexpr double kMaxMisalignedFraction = 0.01;
constexpr double kMaxUnmatchedFraction = 0.03; // codec noise on dense text rows

struct Run {
    QImage page;
    Trajectory trajectory;
    AnalysisResult analysis;
    RenderResult render;
    QString path;
};

Run runEndToEnd(const QString& path, const PageSpec& spec, const Trajectory& base, const Disturbances& d,
                const LongshotOptions& options, QString* error)
{
    Run run;
    run.page = renderPage(spec);
    run.trajectory = clampTrajectory(base, run.page.height(), kViewport.height());
    std::vector<QImage> frames;
    for (size_t i = 0; i < run.trajectory.offsets.size(); ++i) frames.push_back(renderFrame(run.page, kViewport, run.trajectory, int(i), d));
    *error = encodeFrames(path, frames, kFrameRate);
    if (!error->isEmpty()) return run;
    run.path = path;
    auto source = FrameReaderLongshotSource::createNative();
    if (!source->open(path, 0, -1, QRect())) { *error = source->lastError(); return run; }
    run.analysis = LongshotPipeline::analyze(*source, path, 0, -1, QRect(), PipelineParams{}, {});
    run.render = LongshotRenderer::render(*source, path, 0, -1, QRect(), run.analysis, options, {});
    return run;
}

void checkAgainstTruth(const Run& run, int headerHeight = 0)
{
    QCOMPARE(int(run.render.error), int(LongshotError::None));
    QCOMPARE(run.render.parts.size(), 1);
    const QImage out = run.render.parts.first();
    const int first = *std::min_element(run.trajectory.offsets.begin(), run.trajectory.offsets.end()) + headerHeight;
    const int last = *std::max_element(run.trajectory.offsets.begin(), run.trajectory.offsets.end()) + kViewport.height() - 1;
    QImage truth = run.page;
    if (run.render.autoCroppedLeft || run.render.autoCroppedRight) {
        truth = truth.copy(run.render.autoCroppedLeft, 0, truth.width() - run.render.autoCroppedLeft - run.render.autoCroppedRight, truth.height());
    }
    const RowMatchReport r = compareWithGroundTruth(out, truth, first, last);
    const double rows = r.outputRows;
    QVERIFY2(r.duplicatedRows <= kMaxDuplicatedFraction * rows, qPrintable(QStringLiteral("duplicated %1 of %2").arg(r.duplicatedRows).arg(rows)));
    QVERIFY2(r.missingRows <= kMaxMissingFraction * (last - first + 1), qPrintable(QStringLiteral("missing %1").arg(r.missingRows)));
    QVERIFY2(r.misalignedRows <= kMaxMisalignedFraction * rows, qPrintable(QStringLiteral("misaligned %1").arg(r.misalignedRows)));
    QVERIFY2(r.unmatchedRows <= kMaxUnmatchedFraction * rows, qPrintable(QStringLiteral("unmatched %1").arg(r.unmatchedRows)));
    QVERIFY(run.render.breakRows.empty());
}

} // namespace

class tst_LongshotRenderer : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void tileAssignmentPrefersStationaryAndCentred();
    void seamsMoveToLowGradientRows();
    void endToEnd_data();
    void endToEnd();
    void renderStationary();
    void stickyHeaderOnceWhenRequested();
    void sidebarIsAutoCropped();
    void heightCapSplitsOrReports();
    void breakLeavesGapReported();

private:
    QTemporaryDir m_dir;
};

void tst_LongshotRenderer::initTestCase()
{
    QVERIFY(m_dir.isValid());
    if (!FrameReaderLongshotSource::createNative()) QSKIP("No frame reader on this platform");
}

void tst_LongshotRenderer::tileAssignmentPrefersStationaryAndCentred()
{
    AnalysisResult a;
    a.frameSize = QSize(640, 480);
    a.frames.resize(3);
    for (auto& f : a.frames) f.validContentRect = QRect(0, 0, 640, 480);
    a.frames[1].stationary = true;
    a.solve.positions = {0, 100, 200};
    LongshotOptions options;
    options.tileRows = 64;
    const auto tiles = LongshotRenderer::assignTiles(a, options, 680, 0);
    QCOMPARE(int(tiles.size()), 11); // ceil(680 / 64)
    // Output rows 100..163 are covered by frames 0 (rows 100-163 of it), 1 (0-63) and 2? no: frame 2 starts at 200.
    // Frame 1 is stationary and the rows sit near its top; frame 0 has them mid-frame. Stationary wins.
    QCOMPARE(tiles[1].outputTop, 64);
    const TileAssignment& t = tiles[2]; // rows 128..191: frames 0 and 1 both cover
    QCOMPARE(t.frameIndex, 1);
    // Rows 640..679 are only covered by frame 2.
    QCOMPARE(tiles.back().frameIndex, 2);
    // No frame covers a tile beyond the content: a 700-row request leaves the last tile unassigned.
    const auto tall = LongshotRenderer::assignTiles(a, options, 760, 0);
    QCOMPARE(tall.back().frameIndex, -1);
}

void tst_LongshotRenderer::seamsMoveToLowGradientRows()
{
    AnalysisResult a;
    a.frameSize = QSize(640, 480);
    a.frames.resize(2);
    for (auto& f : a.frames) {
        f.validContentRect = QRect(0, 0, 640, 480);
        f.rowGradient.assign(480, 10.0f);
    }
    a.solve.positions = {0, 32};
    // Frame 0 has a flat (gap) row at output row 70; the tile boundary at 64 should move there.
    a.frames[0].rowGradient[70] = 0.0f;
    std::vector<TileAssignment> tiles = {{0, 64, 0}, {64, 128, 1}};
    LongshotRenderer::placeSeams(tiles, a, 0);
    QCOMPARE(tiles[0].outputBottom, 70);
    QCOMPARE(tiles[1].outputTop, 70);
}

void tst_LongshotRenderer::endToEnd_data()
{
    QTest::addColumn<int>("kind");
    QTest::newRow("constant") << 0;
    QTest::newRow("fling") << 1;
    QTest::newRow("back-and-forth") << 2;
    QTest::newRow("pauses") << 3;
    QTest::newRow("hover") << 4;
    QTest::newRow("lazy load") << 5;
}

void tst_LongshotRenderer::endToEnd()
{
    QFETCH(int, kind);
    Trajectory t;
    Disturbances d;
    switch (kind) {
    case 0: t = constantSpeed(60, 0, 45); break;
    case 1: t = fling(50, 200, 140); break;
    case 2: t = backAndForth(70, 900, 600, 50); break;
    case 3: t = withPauses(constantSpeed(40, 0, 45), 5, 3); break;
    case 4: t = constantSpeed(60, 0, 45); d.hoverChange = true; break;
    default: t = constantSpeed(60, 0, 45); d.lazyLoadRow = 1400; d.lazyLoadFrame = 20; break;
    }
    QString error;
    const Run run = runEndToEnd(m_dir.filePath(QStringLiteral("e2e-%1.mp4").arg(kind)), PageSpec{}, t, d, LongshotOptions{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    checkAgainstTruth(run);
    QCOMPARE(run.render.fullHeightPx, run.render.parts.first().height());
    QVERIFY(!run.render.stickyHeaderIncluded);
}

void tst_LongshotRenderer::renderStationary()
{
    QString error;
    const Run run = runEndToEnd(m_dir.filePath(QStringLiteral("still.mp4")), PageSpec{}, constantSpeed(30, 500, 0), Disturbances{}, LongshotOptions{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(int(run.render.error), int(LongshotError::None));
    QCOMPARE(run.render.parts.first().size(), kViewport);
    QVERIFY(run.render.breakRows.empty());
}

void tst_LongshotRenderer::stickyHeaderOnceWhenRequested()
{
    Disturbances d;
    d.stickyHeaderHeight = 40;
    QString error;
    // Default: header excluded entirely; the content below it is stitched.
    const Run without = runEndToEnd(m_dir.filePath(QStringLiteral("header-off.mp4")), PageSpec{}, constantSpeed(60, 0, 45), d, LongshotOptions{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    checkAgainstTruth(without, 40);
    QVERIFY(!without.render.stickyHeaderIncluded);
    // Requested: header appears exactly once at the top, output is taller by the band.
    LongshotOptions options;
    options.includeStickyHeader = true;
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source->open(without.path, 0, -1, QRect()));
    const RenderResult with = LongshotRenderer::render(*source, without.path, 0, -1, QRect(), without.analysis, options, {});
    QVERIFY(with.stickyHeaderIncluded);
    QCOMPARE(with.parts.first().height(), without.render.parts.first().height() + 40);
    // The header rows do not recur further down: compare row profiles of the first 40 rows against all later rows.
    const QImage out = with.parts.first();
    const QImage header = out.copy(0, 0, out.width(), 40);
    int repeats = 0;
    for (int y = 40; y + 40 <= out.height(); y += 4) {
        if (out.copy(0, y, out.width(), 40) == header) ++repeats;
    }
    QCOMPARE(repeats, 0);
}

void tst_LongshotRenderer::sidebarIsAutoCropped()
{
    Disturbances d;
    d.sidebarWidth = 100;
    QString error;
    const Run run = runEndToEnd(m_dir.filePath(QStringLiteral("sidebar.mp4")), PageSpec{}, constantSpeed(60, 0, 45), d, LongshotOptions{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY2(run.render.autoCroppedRight >= 96 && run.render.autoCroppedRight <= 104, qPrintable(QString::number(run.render.autoCroppedRight)));
    QCOMPARE(run.render.parts.first().width(), kViewport.width() - run.render.autoCroppedLeft - run.render.autoCroppedRight);
    checkAgainstTruth(run);
}

void tst_LongshotRenderer::heightCapSplitsOrReports()
{
    QString error;
    LongshotOptions capped;
    capped.maxHeightPx = 1000;
    const Run run = runEndToEnd(m_dir.filePath(QStringLiteral("cap.mp4")), PageSpec{}, constantSpeed(60, 0, 45), Disturbances{}, capped, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QVERIFY(run.render.fullHeightPx > 1000);
    QVERIFY(run.render.heightCapped);
    QCOMPARE(run.render.parts.size(), 1);
    QCOMPARE(run.render.parts.first().height(), 1000);
    LongshotOptions split = capped;
    split.splitOversize = true;
    auto source = FrameReaderLongshotSource::createNative();
    QVERIFY(source->open(run.path, 0, -1, QRect()));
    const RenderResult parts = LongshotRenderer::render(*source, run.path, 0, -1, QRect(), run.analysis, split, {});
    QVERIFY(!parts.heightCapped);
    QCOMPARE(parts.parts.size(), (run.render.fullHeightPx + 999) / 1000);
    int total = 0;
    for (const QImage& p : parts.parts) { QVERIFY(p.height() <= 1000); total += p.height(); }
    QCOMPARE(total, run.render.fullHeightPx);
}

void tst_LongshotRenderer::breakLeavesGapReported()
{
    PageSpec spec;
    spec.height = 6000;
    spec.blankTop = 2000;
    spec.blankHeight = 1400;
    QString error;
    const Run run = runEndToEnd(m_dir.filePath(QStringLiteral("gap.mp4")), spec, constantSpeed(90, 800, 50), Disturbances{}, LongshotOptions{}, &error);
    QVERIFY2(error.isEmpty(), qPrintable(error));
    QCOMPARE(int(run.render.error), int(LongshotError::None));
    // Only the kept island is rendered; the analysis break is passed through so
    // the UI can mark where the recording could not be joined.
    QVERIFY(!run.analysis.solve.breakTimesMs.empty());
    QVERIFY(run.render.parts.first().height() < 6000);
}

QTEST_MAIN(tst_LongshotRenderer)
#include "tst_LongshotRenderer.moc"
```

`tests/CMakeLists.txt`:

```cmake
add_executable(Longshot_Renderer Longshot/tst_LongshotRenderer.cpp)
target_link_libraries(Longshot_Renderer PRIVATE Longshot_SyntheticScroll Qt6::Test
    "$<$<PLATFORM_ID:Windows>:mfreadwrite>" "$<$<PLATFORM_ID:Windows>:mfplat>"
    "$<$<PLATFORM_ID:Windows>:mfuuid>" "$<$<PLATFORM_ID:Windows>:ole32>")
add_test(NAME Longshot_Renderer COMMAND Longshot_Renderer)
set_tests_properties(Longshot_Renderer PROPERTIES TIMEOUT 600 LABELS "integration;slow")
```

- [ ] **Step 2: Run the tests to verify they fail**

Build `Longshot_Renderer`. Expected: `Cannot open include file: 'longshot/LongshotRenderer.h'`.

- [ ] **Step 3: Write the header**

`include/longshot/LongshotRenderer.h`:

```cpp
#pragma once

#include "longshot/LongshotFrameSource.h"
#include "longshot/LongshotPipeline.h"
#include "longshot/LongshotTypes.h"

#include <QImage>
#include <QList>

#include <vector>

namespace SnapTray::Longshot {

// Edges below this confidence mark the rows they place as low confidence.
constexpr double kLowConfidence = 0.5;

struct RenderResult {
    LongshotError error = LongshotError::None;
    QList<QImage> parts;            // one image unless options.splitOversize
    int fullHeightPx = 0;           // height before any cap or split
    bool heightCapped = false;
    int autoCroppedLeft = 0;        // static side columns removed
    int autoCroppedRight = 0;
    std::vector<int> breakRows;     // output rows left empty because no frame covered them reliably
    std::vector<int> lowConfidenceRows;
    bool stickyHeaderIncluded = false;
};

struct TileAssignment {
    int outputTop = 0;
    int outputBottom = 0; // exclusive
    int frameIndex = -1;  // -1 = no reliable coverage
};

// Pass 2. Decodes the range once more sequentially and paints each tile from
// its chosen frame's valid pixels only.
class LongshotRenderer
{
public:
    static RenderResult render(LongshotFrameSource& source, const QString& path, qint64 startMs, qint64 endMs,
                               const QRect& crop, const AnalysisResult& analysis, const LongshotOptions& options,
                               const ProgressFn& progress);

    // One frame per tile: must cover the tile with valid rows; scored by
    // stationary (+2), keyframe (+1), tile centre near frame centre (+0..1),
    // later frames win ties; frames whose thumbnail disagrees with the
    // majority of candidates are rejected.
    static std::vector<TileAssignment> assignTiles(const AnalysisResult& analysis, const LongshotOptions& options,
                                                   int outputHeight, int minPosition);

    // Moves each boundary between different frames to the lowest-gradient
    // row within kSeamSearchRows of it (gradient of the frame above).
    static void placeSeams(std::vector<TileAssignment>& tiles, const AnalysisResult& analysis, int minPosition);
};

} // namespace SnapTray::Longshot
```

- [ ] **Step 4: Write the implementation**

`src/longshot/LongshotRenderer.cpp`:

```cpp
#include "longshot/LongshotRenderer.h"

#include <QDebug>
#include <QPainter>

#include <algorithm>
#include <cmath>
#include <map>
#include <numeric>

namespace SnapTray::Longshot {

namespace {

constexpr double kStationaryBonus = 2.0;
constexpr double kKeyFrameBonus = 1.0;
constexpr double kLaterTieBreak = 0.01;
constexpr int kSeamSearchRows = 8;
constexpr double kMajorityDiffThreshold = 12.0; // mean |luma| difference on thumbnails
constexpr int kMinMajority = 3;

int clampInt(int v, int lo, int hi) { return std::max(lo, std::min(v, hi)); }

// Output rows [top, bottom) of frame i, valid rows only.
bool frameCoverage(const AnalysisResult& a, int i, int minPosition, int* top, int* bottom)
{
    if (!a.solve.positions[i].has_value()) return false;
    const QRect valid = a.frames[i].validContentRect;
    if (!valid.isValid()) return false;
    const int pos = *a.solve.positions[i] - minPosition;
    *top = pos + valid.top();
    *bottom = pos + valid.top() + valid.height();
    return true;
}

// Mean |difference| of two frames' thumbnails over the output rows [top, bottom).
double thumbnailDisagreement(const AnalysisResult& a, int i, int j, int top, int bottom, int minPosition)
{
    const QImage& ti = a.thumbnails[i];
    const QImage& tj = a.thumbnails[j];
    if (ti.isNull() || tj.isNull() || a.frameSize.height() == 0) return 0.0;
    const double scale = double(ti.height()) / a.frameSize.height();
    const int posI = *a.solve.positions[i] - minPosition;
    const int posJ = *a.solve.positions[j] - minPosition;
    double sum = 0.0;
    int count = 0;
    for (int y = top; y < bottom; y += 2) {
        const int yi = clampInt(int((y - posI) * scale), 0, ti.height() - 1);
        const int yj = clampInt(int((y - posJ) * scale), 0, tj.height() - 1);
        const uchar* ri = ti.constScanLine(yi);
        const uchar* rj = tj.constScanLine(yj);
        for (int x = 0; x < ti.width(); ++x) { sum += std::abs(int(ri[x]) - int(rj[x])); ++count; }
    }
    return count ? sum / count : 0.0;
}

} // namespace

std::vector<TileAssignment> LongshotRenderer::assignTiles(const AnalysisResult& a, const LongshotOptions& options,
                                                          int outputHeight, int minPosition)
{
    std::vector<TileAssignment> tiles;
    const int tileRows = std::max(1, options.tileRows);
    const int frameCount = int(a.frames.size());
    for (int top = 0; top < outputHeight; top += tileRows) {
        TileAssignment tile;
        tile.outputTop = top;
        tile.outputBottom = std::min(outputHeight, top + tileRows);
        std::vector<std::pair<double, int>> candidates;
        for (int i = 0; i < frameCount; ++i) {
            int fTop = 0, fBottom = 0;
            if (!frameCoverage(a, i, minPosition, &fTop, &fBottom)) continue;
            if (fTop > tile.outputTop || fBottom < tile.outputBottom) continue;
            const double frameCentre = (fTop + fBottom) / 2.0;
            const double tileCentre = (tile.outputTop + tile.outputBottom) / 2.0;
            const double centrality = 1.0 - std::min(1.0, std::abs(tileCentre - frameCentre) / std::max(1, fBottom - fTop));
            double score = centrality + (a.frames[i].stationary ? kStationaryBonus : 0.0)
                           + (a.frames[i].keyFrame ? kKeyFrameBonus : 0.0) + kLaterTieBreak * i / std::max(1, frameCount);
            candidates.push_back({score, i});
        }
        if (!candidates.empty()) {
            // Majority check: drop candidates that disagree with most others
            // (only when thumbnails exist for every frame).
            if (int(candidates.size()) >= kMinMajority && a.thumbnails.size() == a.frames.size()) {
                std::vector<std::pair<double, int>> kept;
                for (const auto& c : candidates) {
                    int agree = 0;
                    for (const auto& o : candidates) {
                        if (o.second == c.second) continue;
                        if (thumbnailDisagreement(a, c.second, o.second, tile.outputTop, tile.outputBottom, minPosition) <= kMajorityDiffThreshold) ++agree;
                    }
                    if (agree * 2 >= int(candidates.size()) - 1) kept.push_back(c);
                }
                if (!kept.empty()) candidates = kept;
            }
            std::sort(candidates.begin(), candidates.end(), [](const auto& l, const auto& r) { return l.first > r.first; });
            tile.frameIndex = candidates.front().second;
        }
        tiles.push_back(tile);
    }
    return tiles;
}

void LongshotRenderer::placeSeams(std::vector<TileAssignment>& tiles, const AnalysisResult& a, int minPosition)
{
    for (size_t k = 0; k + 1 < tiles.size(); ++k) {
        TileAssignment& above = tiles[k];
        TileAssignment& below = tiles[k + 1];
        if (above.frameIndex < 0 || below.frameIndex < 0 || above.frameIndex == below.frameIndex) continue;
        const FrameFeatures& f = a.frames[above.frameIndex];
        const int pos = *a.solve.positions[above.frameIndex] - minPosition;
        int fTop = 0, fBottom = 0;
        if (!frameCoverage(a, above.frameIndex, minPosition, &fTop, &fBottom)) continue;
        int bTop = 0, bBottom = 0;
        if (!frameCoverage(a, below.frameIndex, minPosition, &bTop, &bBottom)) continue;
        const int boundary = above.outputBottom;
        // The seam may move only where both frames still cover the rows.
        const int lo = std::max({boundary - kSeamSearchRows, above.outputTop + 1, bTop});
        const int hi = std::min({boundary + kSeamSearchRows, below.outputBottom - 1, fBottom});
        int best = boundary;
        float bestGradient = std::numeric_limits<float>::max();
        for (int y = lo; y <= hi; ++y) {
            const int row = y - pos;
            if (row < 0 || row >= int(f.rowGradient.size())) continue;
            if (f.rowGradient[row] < bestGradient) { bestGradient = f.rowGradient[row]; best = y; }
        }
        above.outputBottom = best;
        below.outputTop = best;
    }
}

RenderResult LongshotRenderer::render(LongshotFrameSource& source, const QString& path, qint64 startMs, qint64 endMs,
                                      const QRect& crop, const AnalysisResult& a, const LongshotOptions& options,
                                      const ProgressFn& progress)
{
    RenderResult result;
    if (a.error != LongshotError::None) { result.error = a.error; return result; }
    const int frameCount = int(a.frames.size());
    if (frameCount == 0 || a.solve.positions.size() != a.frames.size()) { result.error = LongshotError::NoReliableContent; return result; }

    // Common column span and vertical extent over placed frames.
    int minPosition = std::numeric_limits<int>::max();
    int maxBottom = std::numeric_limits<int>::min();
    QRect columns(0, 0, a.frameSize.width(), 1);
    int firstPlaced = -1;
    for (int i = 0; i < frameCount; ++i) {
        if (!a.solve.positions[i].has_value() || !a.frames[i].validContentRect.isValid()) continue;
        if (firstPlaced < 0) firstPlaced = i;
        minPosition = std::min(minPosition, *a.solve.positions[i]);
        maxBottom = std::max(maxBottom, *a.solve.positions[i] + a.frameSize.height());
        const QRect v = a.frames[i].validContentRect;
        columns.setLeft(std::max(columns.left(), v.left()));
        columns.setRight(std::min(columns.right(), v.right()));
    }
    if (firstPlaced < 0 || columns.width() <= 0) { result.error = LongshotError::NoReliableContent; return result; }
    result.autoCroppedLeft = columns.left();
    result.autoCroppedRight = a.frameSize.width() - 1 - columns.right();

    // Sticky header: the first placed frame's top band, once, when requested.
    const int headerRows = (options.includeStickyHeader && a.frames[firstPlaced].excludedBands.top > 0)
                               ? a.frames[firstPlaced].excludedBands.top : 0;
    result.stickyHeaderIncluded = headerRows > 0;

    // Rows between the top-most valid row and the bottom-most valid row.
    int contentTop = std::numeric_limits<int>::max();
    int contentBottom = std::numeric_limits<int>::min();
    for (int i = 0; i < frameCount; ++i) {
        int t = 0, b = 0;
        if (frameCoverage(a, i, minPosition, &t, &b)) { contentTop = std::min(contentTop, t); contentBottom = std::max(contentBottom, b); }
    }
    const int bodyHeight = contentBottom - contentTop;
    std::vector<TileAssignment> tiles = assignTiles(a, options, contentBottom, minPosition);
    // Drop tiles above the first valid row (header area excluded everywhere).
    tiles.erase(std::remove_if(tiles.begin(), tiles.end(), [&](const TileAssignment& t) { return t.outputBottom <= contentTop; }), tiles.end());
    if (!tiles.empty() && tiles.front().outputTop < contentTop) tiles.front().outputTop = contentTop;
    placeSeams(tiles, a, minPosition);

    result.fullHeightPx = headerRows + bodyHeight;
    const int outputHeight = result.fullHeightPx;
    int renderHeight = outputHeight;
    if (!options.splitOversize && outputHeight > options.maxHeightPx) {
        renderHeight = options.maxHeightPx;
        result.heightCapped = true;
    }
    QImage canvas(columns.width(), renderHeight, QImage::Format_RGB32);
    canvas.fill(Qt::white);

    // Which frames paint which output row ranges.
    std::map<int, std::vector<std::pair<int, int>>> ranges; // frame -> [(outTop, outBottom)]
    for (const TileAssignment& t : tiles) {
        if (t.outputBottom <= t.outputTop) continue;
        if (t.frameIndex < 0) {
            for (int y = t.outputTop; y < t.outputBottom; ++y) result.breakRows.push_back(headerRows + y - contentTop);
            continue;
        }
        ranges[t.frameIndex].push_back({t.outputTop, t.outputBottom});
    }
    if (headerRows > 0) ranges[firstPlaced].push_back({-headerRows, 0}); // sentinel: header band

    // Low-confidence rows: frames placed only through weak edges.
    std::vector<double> bestEdgeConfidence(frameCount, 0.0);
    for (const PairShift& e : a.edges) {
        bestEdgeConfidence[e.to] = std::max(bestEdgeConfidence[e.to], e.confidence);
        bestEdgeConfidence[e.from] = std::max(bestEdgeConfidence[e.from], e.confidence);
    }

    if (!source.open(path, startMs, endMs, crop)) { result.error = LongshotError::SourceUnavailable; return result; }
    QPainter painter(&canvas);
    int index = 0;
    qint64 tMs = 0;
    const int expected = std::max(1, source.expectedFrameCount());
    while (auto frame = source.next(&tMs)) {
        const auto it = ranges.find(index);
        if (it != ranges.end()) {
            const int pos = *a.solve.positions[index] - minPosition;
            for (const auto& [outTop, outBottom] : it->second) {
                if (outTop < 0) {
                    // Header band from the frame's excluded top rows.
                    painter.drawImage(QPoint(0, 0), *frame, QRect(columns.left(), 0, columns.width(), headerRows));
                    continue;
                }
                const int srcTop = outTop - pos;
                const int destTop = headerRows + outTop - contentTop;
                const int height = outBottom - outTop;
                if (destTop >= renderHeight) continue;
                const int clippedHeight = std::min(height, renderHeight - destTop);
                painter.drawImage(QPoint(0, destTop), *frame, QRect(columns.left(), srcTop, columns.width(), clippedHeight));
                if (bestEdgeConfidence[index] < kLowConfidence && index != firstPlaced) {
                    for (int y = destTop; y < destTop + clippedHeight; ++y) result.lowConfidenceRows.push_back(y);
                }
            }
        }
        ++index;
        if (progress && !progress(std::min(99, int(qint64(index) * 100 / expected)))) { result.error = LongshotError::Cancelled; return result; }
    }
    painter.end();

    if (options.splitOversize && outputHeight > options.maxHeightPx) {
        for (int top = 0; top < outputHeight; top += options.maxHeightPx) {
            result.parts.push_back(canvas.copy(0, top, canvas.width(), std::min(options.maxHeightPx, outputHeight - top)));
        }
    } else {
        result.parts.push_back(canvas);
    }
    std::sort(result.breakRows.begin(), result.breakRows.end());
    if (progress) progress(100);
    qDebug() << "LongshotRenderer: height" << result.fullHeightPx << "parts" << result.parts.size() << "capped" << result.heightCapped
             << "side crop" << result.autoCroppedLeft << result.autoCroppedRight << "break rows" << result.breakRows.size();
    return result;
}

} // namespace SnapTray::Longshot
```

Add `src/longshot/LongshotRenderer.cpp` to the `snaptray_algorithms` source list.

- [ ] **Step 5: Run the tests to verify they pass**

Build and run `Longshot_Renderer` (`-o <scratch>/t6.txt,txt`; several minutes). Expected: all slots pass. If `stickyHeaderOnceWhenRequested` finds the header repeated, tiles were assigned from rows inside an excluded band — `frameCoverage` must use `validContentRect`, which Task 5 shrinks by the bands. If row metrics exceed the 1 % bounds on `back-and-forth`, check `kMajorityDiffThreshold` against the measured thumbnail noise before changing the bounds; report the measured numbers.

- [ ] **Step 6: Commit**

```bash
git add include/longshot/LongshotRenderer.h src/longshot/LongshotRenderer.cpp tests/Longshot/tst_LongshotRenderer.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(longshot): render solved frames into a tall image"
```

---
### Task 7: LongshotSession — the cache contract

**Files:**
- Create: `include/longshot/LongshotSession.h`, `src/longshot/LongshotSession.cpp`
- Create: `tests/Longshot/tst_LongshotSession.cpp`
- Modify: `CMakeLists.txt`, `tests/CMakeLists.txt`

**Interfaces:**
- Consumes: `LongshotPipeline::analyze`, `LongshotRenderer::render`, `LongshotFrameSource`, `VideoCropGeometry::normalizeCropRect`.
- Produces (namespace `SnapTray::Longshot`):
  - `struct SourceIdentity { QString path; qint64 sizeBytes = 0; qint64 modifiedMsSinceEpoch = 0; static SourceIdentity fromFile(const QString& path); bool operator==(const SourceIdentity&) const; }`
  - `struct AnalysisKey { SourceIdentity source; QRect normalizedCrop; qint64 startMs = 0; qint64 endMs = -1; int analysisVersion = kAnalysisVersion; bool sameSourceAndCrop(const AnalysisKey&) const; bool operator==(const AnalysisKey&) const; }`
  - `class DecodedFrameCache` — `explicit DecodedFrameCache(qint64 budgetBytes)`, `std::optional<QImage> find(const SourceIdentity&, qint64 tMs) const`, `void store(const SourceIdentity&, qint64 tMs, const QImage&)`, `qint64 bytes() const`, `int count() const`, `void clear()`. LRU by bytes; a frame larger than the budget is never stored.
  - `struct RunReport { LongshotError error = LongshotError::None; bool reusedFeatures = false; bool reusedSolve = false; bool reusedRender = false; int framesAnalyzed = 0; AnalysisResult analysis; RenderResult render; }`
  - `class LongshotSession` — `using SourceFactory = std::function<std::unique_ptr<LongshotFrameSource>()>;` `LongshotSession(SourceFactory factory, qint64 decodeCacheBudgetBytes = kDecodeCacheBudgetBytes)`; `void setRecording(const QString& path)`, `void setCrop(const QRect& crop)` (video pixels; normalized against the frame size on run), `void setTrim(qint64 startMs, qint64 endMs)`, `void setOptions(const LongshotOptions&)`, `void setPipelineParams(const PipelineParams&)`; `RunReport run(const ProgressFn& progress)`; `const DecodedFrameCache& decodeCache() const`. `constexpr qint64 kDecodeCacheBudgetBytes = 128 * 1024 * 1024`.
  - Invalidation rules implemented by `run()`: same key + same options → `reusedFeatures = reusedSolve = reusedRender = true` and no decode; options change only → reuse analysis, re-render; trim change with same source+crop → keep `FrameFeatures`/thumbnails for frames with `tMs` inside the new range (`reusedFeatures = true`), analyse only newly included frames, rebuild edges, re-solve, re-render; crop or source change → everything recomputed.

- [ ] **Step 1: Write the failing tests**

`tests/Longshot/tst_LongshotSession.cpp` (uses an in-memory fake source so no encoder is needed):

```cpp
#include "SyntheticScroll.h"
#include "longshot/LongshotSession.h"

#include <QtTest>
#include <QTemporaryDir>

using namespace SnapTray::Longshot;
using namespace SyntheticScroll;

namespace {

// Serves pre-rendered frames at 50 ms intervals and counts decodes.
class FakeSource final : public LongshotFrameSource
{
public:
    FakeSource(std::shared_ptr<std::vector<QImage>> frames, std::shared_ptr<int> decodes) : m_frames(frames), m_decodes(decodes) {}
    bool open(const QString&, qint64 startMs, qint64 endMs, const QRect& crop) override
    {
        m_crop = crop.isEmpty() ? m_frames->front().rect() : crop;
        m_start = int(startMs / 50);
        m_end = endMs < 0 ? int(m_frames->size()) : int(std::min<qint64>(m_frames->size(), (endMs + 49) / 50));
        m_next = m_start;
        return true;
    }
    std::optional<QImage> next(qint64* tMs) override
    {
        if (m_next >= m_end) return std::nullopt;
        ++*m_decodes;
        if (tMs) *tMs = qint64(m_next) * 50;
        return (*m_frames)[size_t(m_next++)].copy(m_crop);
    }
    QSize frameSize() const override { return m_crop.size(); }
    QSize videoSize() const override { return m_frames->front().size(); }
    double frameRate() const override { return 20.0; }
    int expectedFrameCount() const override { return m_end - m_start; }
    QString lastError() const override { return {}; }
private:
    std::shared_ptr<std::vector<QImage>> m_frames;
    std::shared_ptr<int> m_decodes;
    QRect m_crop;
    int m_start = 0, m_end = 0, m_next = 0;
};

} // namespace

class tst_LongshotSession : public QObject
{
    Q_OBJECT
private slots:
    void initTestCase();
    void decodeCacheIsBoundedLru();
    void decodeCacheRequiresSameIdentity();
    void identicalRunReusesEverything();
    void optionsChangeReusesAnalysis();
    void trimExtensionReusesOverlapFeatures();
    void cropChangeInvalidatesAll();
    void sourceChangeInvalidatesAll();

private:
    std::shared_ptr<std::vector<QImage>> m_frames = std::make_shared<std::vector<QImage>>();
    std::shared_ptr<int> m_decodes = std::make_shared<int>(0);
    QTemporaryDir m_dir;
    QString m_fileA;
    QString m_fileB;

    LongshotSession makeSession()
    {
        return LongshotSession([this]() { return std::make_unique<FakeSource>(m_frames, m_decodes); });
    }
};

void tst_LongshotSession::initTestCase()
{
    QVERIFY(m_dir.isValid());
    const QImage page = renderPage(PageSpec{});
    const Trajectory t = clampTrajectory(constantSpeed(40, 0, 45), page.height(), kViewport.height());
    for (int i = 0; i < 40; ++i) m_frames->push_back(renderFrame(page, kViewport, t, i, Disturbances{}));
    // Source identity comes from the file, so two distinct files stand in for two recordings.
    m_fileA = m_dir.filePath(QStringLiteral("a.mp4"));
    m_fileB = m_dir.filePath(QStringLiteral("b.mp4"));
    QFile a(m_fileA); QVERIFY(a.open(QIODevice::WriteOnly)); a.write("aaaa"); a.close();
    QFile b(m_fileB); QVERIFY(b.open(QIODevice::WriteOnly)); b.write("bbbbbbbb"); b.close();
}

void tst_LongshotSession::decodeCacheIsBoundedLru()
{
    const SourceIdentity id = SourceIdentity::fromFile(m_fileA);
    QImage frame(100, 100, QImage::Format_RGB32); // 40,000 bytes
    DecodedFrameCache cache(100000);                 // room for two
    cache.store(id, 0, frame);
    cache.store(id, 50, frame);
    QCOMPARE(cache.count(), 2);
    QVERIFY(cache.find(id, 0).has_value());        // touch 0 -> 50 is now least recent
    cache.store(id, 100, frame);
    QCOMPARE(cache.count(), 2);
    QVERIFY(cache.find(id, 0).has_value());
    QVERIFY(!cache.find(id, 50).has_value());
    QVERIFY(cache.find(id, 100).has_value());
    QVERIFY(cache.bytes() <= 100000);
    QImage huge(300, 300, QImage::Format_RGB32);     // 360,000 bytes > budget
    cache.store(id, 150, huge);
    QVERIFY(!cache.find(id, 150).has_value());
    cache.clear();
    QCOMPARE(cache.count(), 0);
    QCOMPARE(cache.bytes(), qint64(0));
}

void tst_LongshotSession::decodeCacheRequiresSameIdentity()
{
    DecodedFrameCache cache(1000000);
    QImage frame(10, 10, QImage::Format_RGB32);
    cache.store(SourceIdentity::fromFile(m_fileA), 0, frame);
    QVERIFY(cache.find(SourceIdentity::fromFile(m_fileA), 0).has_value());
    QVERIFY(!cache.find(SourceIdentity::fromFile(m_fileB), 0).has_value());
    QVERIFY(!cache.find(SourceIdentity::fromFile(m_fileA), 50).has_value());
}

void tst_LongshotSession::identicalRunReusesEverything()
{
    LongshotSession session = makeSession();
    session.setRecording(m_fileA);
    *m_decodes = 0;
    const RunReport first = session.run({});
    QCOMPARE(int(first.error), int(LongshotError::None));
    QVERIFY(first.framesAnalyzed == 40);
    QVERIFY(*m_decodes > 0);
    const int decodesAfterFirst = *m_decodes;
    const RunReport second = session.run({});
    QVERIFY(second.reusedFeatures);
    QVERIFY(second.reusedSolve);
    QVERIFY(second.reusedRender);
    QCOMPARE(*m_decodes, decodesAfterFirst);
    QCOMPARE(second.render.parts.first(), first.render.parts.first());
}

void tst_LongshotSession::optionsChangeReusesAnalysis()
{
    LongshotSession session = makeSession();
    session.setRecording(m_fileA);
    session.run({});
    *m_decodes = 0;
    LongshotOptions options;
    options.maxHeightPx = 500;
    session.setOptions(options);
    const RunReport report = session.run({});
    QVERIFY(report.reusedFeatures);
    QVERIFY(report.reusedSolve);
    QVERIFY(!report.reusedRender);
    QCOMPARE(report.framesAnalyzed, 0);
    QVERIFY(report.render.heightCapped);
    QCOMPARE(*m_decodes, 40); // one render pass, no analysis pass
}

void tst_LongshotSession::trimExtensionReusesOverlapFeatures()
{
    LongshotSession session = makeSession();
    session.setRecording(m_fileA);
    session.setTrim(0, 1000); // frames 0..19
    const RunReport first = session.run({});
    QCOMPARE(first.framesAnalyzed, 20);
    session.setTrim(0, 2000); // frames 0..39
    *m_decodes = 0;
    const RunReport extended = session.run({});
    QVERIFY(extended.reusedFeatures);
    QVERIFY(!extended.reusedSolve);
    QVERIFY(!extended.reusedRender);
    QCOMPARE(extended.framesAnalyzed, 20); // only the newly included frames
    QCOMPARE(extended.analysis.frames.size(), size_t(40));
    for (size_t i = 0; i < 40; ++i) QCOMPARE(extended.analysis.frames[i].tMs, qint64(i) * 50);
    QVERIFY(extended.render.parts.first().height() > first.render.parts.first().height());
    // Shrinking the trim drops out-of-range frames without re-analysing anything.
    session.setTrim(500, 1500);
    *m_decodes = 0;
    const RunReport shrunk = session.run({});
    QVERIFY(shrunk.reusedFeatures);
    QCOMPARE(shrunk.framesAnalyzed, 0);
    QCOMPARE(shrunk.analysis.frames.size(), size_t(20));
    QCOMPARE(shrunk.analysis.frames.front().tMs, qint64(500));
}

void tst_LongshotSession::cropChangeInvalidatesAll()
{
    LongshotSession session = makeSession();
    session.setRecording(m_fileA);
    session.run({});
    session.setCrop(QRect(0, 0, 400, 480));
    *m_decodes = 0;
    const RunReport report = session.run({});
    QVERIFY(!report.reusedFeatures);
    QVERIFY(!report.reusedSolve);
    QVERIFY(!report.reusedRender);
    QCOMPARE(report.framesAnalyzed, 40);
    QCOMPARE(report.analysis.frameSize, QSize(400, 480));
    QCOMPARE(report.render.parts.first().width(), 400 - report.render.autoCroppedLeft - report.render.autoCroppedRight);
}

void tst_LongshotSession::sourceChangeInvalidatesAll()
{
    LongshotSession session = makeSession();
    session.setRecording(m_fileA);
    session.run({});
    session.setRecording(m_fileB);
    const RunReport report = session.run({});
    QVERIFY(!report.reusedFeatures);
    QCOMPARE(report.framesAnalyzed, 40);
}

QTEST_MAIN(tst_LongshotSession)
#include "tst_LongshotSession.moc"
```

`tests/CMakeLists.txt`:

```cmake
add_executable(Longshot_Session Longshot/tst_LongshotSession.cpp)
target_link_libraries(Longshot_Session PRIVATE Longshot_SyntheticScroll Qt6::Test)
add_test(NAME Longshot_Session COMMAND Longshot_Session)
set_tests_properties(Longshot_Session PROPERTIES TIMEOUT 300 LABELS "unit")
```

- [ ] **Step 2: Run the tests to verify they fail**

Build `Longshot_Session`. Expected: `Cannot open include file: 'longshot/LongshotSession.h'`.

- [ ] **Step 3: Write the header**

`include/longshot/LongshotSession.h`:

```cpp
#pragma once

#include "longshot/LongshotFrameSource.h"
#include "longshot/LongshotPipeline.h"
#include "longshot/LongshotRenderer.h"
#include "longshot/LongshotTypes.h"

#include <QImage>
#include <QRect>
#include <QString>

#include <functional>
#include <list>
#include <memory>
#include <optional>

namespace SnapTray::Longshot {

constexpr qint64 kDecodeCacheBudgetBytes = qint64(128) * 1024 * 1024;

struct SourceIdentity {
    QString path;
    qint64 sizeBytes = 0;
    qint64 modifiedMsSinceEpoch = 0;
    static SourceIdentity fromFile(const QString& path);
    bool operator==(const SourceIdentity& o) const
    {
        return path == o.path && sizeBytes == o.sizeBytes && modifiedMsSinceEpoch == o.modifiedMsSinceEpoch;
    }
    bool operator!=(const SourceIdentity& o) const { return !(*this == o); }
};

struct AnalysisKey {
    SourceIdentity source;
    QRect normalizedCrop;
    qint64 startMs = 0;
    qint64 endMs = -1;
    int analysisVersion = kAnalysisVersion;
    bool sameSourceAndCrop(const AnalysisKey& o) const
    {
        return source == o.source && normalizedCrop == o.normalizedCrop && analysisVersion == o.analysisVersion;
    }
    bool operator==(const AnalysisKey& o) const { return sameSourceAndCrop(o) && startMs == o.startMs && endMs == o.endMs; }
};

// Bounded LRU of decoded full frames keyed by (source identity, media time).
// Separate from analysis: crop changes do not touch it.
class DecodedFrameCache
{
public:
    explicit DecodedFrameCache(qint64 budgetBytes);
    std::optional<QImage> find(const SourceIdentity& source, qint64 tMs) const;
    void store(const SourceIdentity& source, qint64 tMs, const QImage& frame);
    qint64 bytes() const { return m_bytes; }
    int count() const { return int(m_entries.size()); }
    void clear();

private:
    struct Entry { SourceIdentity source; qint64 tMs; QImage frame; };
    mutable std::list<Entry> m_entries; // front = most recent
    qint64 m_budgetBytes;
    qint64 m_bytes = 0;
};

struct RunReport {
    LongshotError error = LongshotError::None;
    bool reusedFeatures = false;
    bool reusedSolve = false;
    bool reusedRender = false;
    int framesAnalyzed = 0;
    AnalysisResult analysis;
    RenderResult render;
};

// Owns the cache contract between the preview's edits and the engine.
class LongshotSession
{
public:
    using SourceFactory = std::function<std::unique_ptr<LongshotFrameSource>()>;

    explicit LongshotSession(SourceFactory factory, qint64 decodeCacheBudgetBytes = kDecodeCacheBudgetBytes);

    void setRecording(const QString& path);
    void setCrop(const QRect& cropVideoPixels);
    void setTrim(qint64 startMs, qint64 endMs);
    void setOptions(const LongshotOptions& options);
    void setPipelineParams(const PipelineParams& params);

    RunReport run(const ProgressFn& progress);

    const DecodedFrameCache& decodeCache() const { return m_decodeCache; }

private:
    SourceFactory m_factory;
    DecodedFrameCache m_decodeCache;
    QString m_path;
    QRect m_crop;
    qint64 m_startMs = 0;
    qint64 m_endMs = -1;
    LongshotOptions m_options;
    PipelineParams m_params;

    std::optional<AnalysisKey> m_analysisKey;
    AnalysisResult m_analysis;
    std::optional<LongshotOptions> m_renderOptions;
    RenderResult m_render;
};

} // namespace SnapTray::Longshot
```

- [ ] **Step 4: Write the implementation**

`src/longshot/LongshotSession.cpp`:

```cpp
#include "longshot/LongshotSession.h"

#include "utils/VideoCropGeometry.h"

#include <QDateTime>
#include <QDebug>
#include <QFileInfo>

#include <algorithm>
#include <limits>

namespace SnapTray::Longshot {

SourceIdentity SourceIdentity::fromFile(const QString& path)
{
    SourceIdentity id;
    id.path = path;
    const QFileInfo info(path);
    if (info.exists()) {
        id.sizeBytes = info.size();
        id.modifiedMsSinceEpoch = info.lastModified().toMSecsSinceEpoch();
    }
    return id;
}

DecodedFrameCache::DecodedFrameCache(qint64 budgetBytes) : m_budgetBytes(budgetBytes) {}

std::optional<QImage> DecodedFrameCache::find(const SourceIdentity& source, qint64 tMs) const
{
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->tMs == tMs && it->source == source) {
            m_entries.splice(m_entries.begin(), m_entries, it); // most recent first
            return m_entries.front().frame;
        }
    }
    return std::nullopt;
}

void DecodedFrameCache::store(const SourceIdentity& source, qint64 tMs, const QImage& frame)
{
    const qint64 size = qint64(frame.sizeInBytes());
    if (size > m_budgetBytes) return; // one frame may not evict the whole cache
    for (auto it = m_entries.begin(); it != m_entries.end(); ++it) {
        if (it->tMs == tMs && it->source == source) {
            m_bytes -= qint64(it->frame.sizeInBytes());
            m_entries.erase(it);
            break;
        }
    }
    while (!m_entries.empty() && m_bytes + size > m_budgetBytes) {
        m_bytes -= qint64(m_entries.back().frame.sizeInBytes());
        m_entries.pop_back();
    }
    m_entries.push_front({source, tMs, frame});
    m_bytes += size;
}

void DecodedFrameCache::clear()
{
    m_entries.clear();
    m_bytes = 0;
}

LongshotSession::LongshotSession(SourceFactory factory, qint64 decodeCacheBudgetBytes)
    : m_factory(std::move(factory)), m_decodeCache(decodeCacheBudgetBytes)
{
}

void LongshotSession::setRecording(const QString& path) { m_path = path; }
void LongshotSession::setCrop(const QRect& crop) { m_crop = crop; }
void LongshotSession::setTrim(qint64 startMs, qint64 endMs) { m_startMs = startMs; m_endMs = endMs; }
void LongshotSession::setOptions(const LongshotOptions& options) { m_options = options; }
void LongshotSession::setPipelineParams(const PipelineParams& params) { m_params = params; }

RunReport LongshotSession::run(const ProgressFn& progress)
{
    RunReport report;
    if (!m_factory) { report.error = LongshotError::SourceUnavailable; return report; }
    std::unique_ptr<LongshotFrameSource> source = m_factory();
    if (!source) { report.error = LongshotError::SourceUnavailable; return report; }
    if (!source->open(m_path, m_startMs, m_endMs, m_crop)) {
        qWarning() << "LongshotSession: open failed:" << source->lastError();
        report.error = source->lastError().contains(QStringLiteral("crop")) ? LongshotError::CropTooSmall
                                                                            : LongshotError::SourceUnavailable;
        return report;
    }

    AnalysisKey key;
    key.source = SourceIdentity::fromFile(m_path);
    key.normalizedCrop = m_crop.isEmpty() ? QRect() : VideoCropGeometry::normalizeCropRect(m_crop, source->videoSize());
    key.startMs = m_startMs;
    key.endMs = m_endMs;

    const bool analysisValid = m_analysisKey.has_value() && m_analysis.error == LongshotError::None;
    const bool sameAnalysis = analysisValid && *m_analysisKey == key;
    const bool sameSourceCrop = analysisValid && m_analysisKey->sameSourceAndCrop(key);

    if (sameAnalysis) {
        report.reusedFeatures = true;
        report.reusedSolve = true;
    } else if (sameSourceCrop) {
        // Trim change: per-frame features inside the new range are reused,
        // only newly included frames are analysed, edges/solve are rebuilt.
        report.reusedFeatures = true;
        const qint64 newEnd = m_endMs < 0 ? std::numeric_limits<qint64>::max() : m_endMs;
        std::vector<FrameFeatures> known;
        std::vector<QImage> knownThumbs;
        for (size_t i = 0; i < m_analysis.frames.size() && i < m_analysis.thumbnails.size(); ++i) {
            const qint64 t = m_analysis.frames[i].tMs;
            if (t >= m_startMs && t < newEnd) {
                known.push_back(m_analysis.frames[i]);
                knownThumbs.push_back(m_analysis.thumbnails[i]);
            }
        }
        m_analysis = LongshotPipeline::analyzeIncremental(*source, m_path, m_startMs, m_endMs, m_crop, m_params, progress,
                                                          known, knownThumbs, &report.framesAnalyzed);
    } else {
        // Source or crop changed: nothing crop-dependent survives.
        m_analysis = LongshotPipeline::analyze(*source, m_path, m_startMs, m_endMs, m_crop, m_params, progress);
        report.framesAnalyzed = int(m_analysis.frames.size());
        m_renderOptions.reset();
    }
    m_analysisKey = key;
    report.analysis = m_analysis;
    if (m_analysis.error != LongshotError::None) {
        report.error = m_analysis.error;
        m_renderOptions.reset();
        return report;
    }

    // Rendered tiles survive only when analysis and options are both unchanged.
    const bool sameRender = sameAnalysis && m_renderOptions.has_value() && *m_renderOptions == m_options
                            && m_render.error == LongshotError::None;
    if (sameRender) {
        report.reusedRender = true;
    } else {
        m_render = LongshotRenderer::render(*source, m_path, m_startMs, m_endMs, m_crop, m_analysis, m_options, progress);
        m_renderOptions = m_options;
    }
    report.render = m_render;
    report.error = m_render.error;
    qDebug() << "LongshotSession: reused features" << report.reusedFeatures << "solve" << report.reusedSolve
             << "render" << report.reusedRender << "analysed" << report.framesAnalyzed;
    return report;
}

} // namespace SnapTray::Longshot
```

Add `src/longshot/LongshotSession.cpp` to the `snaptray_algorithms` source list. The decode cache is constructed and exposed here so Phase 4b's preview integration can feed it from the player's decoded frames; the engine's own passes stream frames and do not populate it (keeping "do not retain all full-resolution frames in RAM" true by construction).

- [ ] **Step 5: Run the tests to verify they pass**

Build and run `Longshot_Session` (`-o <scratch>/t7.txt,txt`). Expected: 8 slots pass. `optionsChangeReusesAnalysis` asserts exactly 40 decodes (one render pass); if the renderer decodes more, it reopened the source twice — fix the renderer, not the number. `trimExtensionReusesOverlapFeatures` asserts `framesAnalyzed == 20` after extending and `0` after shrinking; both come from `analyzeIncremental`'s counter.

- [ ] **Step 6: Commit**

```bash
git add include/longshot/LongshotSession.h src/longshot/LongshotSession.cpp tests/Longshot/tst_LongshotSession.cpp CMakeLists.txt tests/CMakeLists.txt
git commit -m "feat(longshot): add the session cache contract"
```

---

## Final verification

- `cmake --build build` (whole tree), then `ctest --test-dir build -j4 -E Annotations_AnnotationLayer` → 100 % (AnnotationLayer is the known five-minute test on this machine). The Longshot integration targets encode many recordings; expect several minutes.
- Measured numbers to record in the final report: per-trajectory placement accuracy (Task 5), row metrics per end-to-end case (Task 6), analysis + render wall time for the 60-frame constant case.
- Open items for the ledger: the macOS path (AVFoundationFrameReader as the longshot source) is exercised only by reading on this host; `IVideoFrameReader` decodes from the start of the file to reach a trim start (acceptable for Phase 4a; a `seekTo` optimisation is Phase 4b material); a header shown in a single frame only cannot be masked by pair analysis (documented limitation; the renderer's majority check covers it); Phase 4b covers the real-recording corpus and evaluation CLI, the Preview "Long Screenshot" UX, translations and docs.
