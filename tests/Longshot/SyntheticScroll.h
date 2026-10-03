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
    // Rows [blankTop, blankTop + blankHeight) are left uniform white (no element
    // overlapping the band is painted); 0 = none. Enabling the band changes the
    // random sequence for later rows, since skipped lines consume no draws.
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
    int duplicatedRows = 0;  // output rows repeating earlier content: matched page row <= highest matched so far
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
// luma profile; ambiguous rows (uniform bands, identical text-line rows) are
// resolved by the path with the fewest discontinuities) and scores the mapping. [firstPageRow, lastPageRow] is the page span the recording
// actually showed, so rows outside it are not counted as missing.
RowMatchReport compareWithGroundTruth(const QImage& result, const QImage& page, int firstPageRow, int lastPageRow);

// 64-bin luma profile distance (mean abs bin difference, 0..255) between row
// ya of a and row yb of b; both must be RGB32.
double rowProfileDistance(const QImage& a, int ya, const QImage& b, int yb);

} // namespace SyntheticScroll
