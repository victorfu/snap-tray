#pragma once

#include "longshot/LongshotFrameSource.h"
#include "longshot/LongshotPipeline.h"
#include "longshot/LongshotTypes.h"

#include <QImage>
#include <QList>

#include <vector>

namespace SnapTray::Longshot {

// Edges below this confidence mark the rows they place as low confidence.
// The analyzer only accepts a pair whose best NCC score reaches minPeakScore
// (0.55) with a margin of minMargin (0.15) over the runner-up, and reports
// confidence = best + margin, so every accepted edge lies in
// [minPeakScore + minMargin, 1.0] = [0.70, 1.0]. A threshold at or below 0.70
// could never fire; 0.85 marks the weaker half of the accepted range. Keep it
// inside that range if AnalyzerParams defaults change.
constexpr double kLowConfidence = 0.85;

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

    // One frame per tile. Candidates must cover the tile with valid rows. They
    // are partitioned into agreeing groups (thumbnails within
    // kMajorityDiffThreshold and full-resolution row means within
    // kRowDeviation on all but a few rows); the group containing the LATEST
    // frame wins (later observations override earlier transients such as lazy
    // placeholders), and within it the best score: stationary (+2), keyframe
    // (+1), tile centre near frame centre (+0..1), later frames win ties.
    static std::vector<TileAssignment> assignTiles(const AnalysisResult& analysis, const LongshotOptions& options,
                                                   int outputHeight, int minPosition);

    // Moves each boundary between different frames to the lowest-gradient
    // row within kSeamSearchRows of it (gradient of the frame above), never
    // across rows on which the two frames' row means disagree.
    static void placeSeams(std::vector<TileAssignment>& tiles, const AnalysisResult& analysis, int minPosition);
};

} // namespace SnapTray::Longshot
