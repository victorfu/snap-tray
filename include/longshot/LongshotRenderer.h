#pragma once

#include "longshot/LongshotFrameSource.h"
#include "longshot/LongshotPipeline.h"
#include "longshot/LongshotTypes.h"

#include <QImage>
#include <QList>

#include <vector>

namespace SnapTray::Longshot {

// Frames without an accepted path of edges at or above this confidence to
// their section anchor have their painted rows marked as low confidence.
// The analyzer only accepts a pair whose best NCC score reaches minPeakScore
// (0.55) with a margin of minMargin (0.15) over the runner-up, and reports
// confidence = best + margin, so every accepted edge lies in
// [minPeakScore + minMargin, 1.0] = [0.70, 1.0]. A threshold at or below 0.70
// could never fire; 0.85 marks the weaker half of the accepted range. Keep it
// inside that range if AnalyzerParams defaults change.
constexpr double kLowConfidence = 0.85;

struct SourceSpan {
    int firstRow = 0;
    int endRow = 0; // exclusive; output coordinates across all parts
    qint64 timeMs = 0;
};

constexpr qint64 kOutputBudgetBytes = 512LL * 1024 * 1024;
struct RenderPartInfo {
    int section = 0;
    int indexInSection = 0;
    int partsInSection = 1;
    qint64 startMs = 0;
    qint64 endMs = 0; // timestamp of the section's last observed frame
    int autoCroppedLeft = 0;
    int autoCroppedRight = 0;
};

struct RenderResult {
    LongshotError error = LongshotError::None;
    int sectionCount = 1;
    QList<RenderPartInfo> partInfo;
    QList<QImage> parts;            // independent sections, each followed by its height-split parts
    int fullHeightPx = 0;           // height before any cap or split
    bool heightCapped = false;
    int autoCroppedLeft = 0;        // static side columns removed
    int autoCroppedRight = 0;
    std::vector<int> breakRows;     // output rows with unfilled pixels because coverage was incomplete
    std::vector<int> lowConfidenceRows;
    std::vector<SourceSpan> sourceSpans; // actual painted observations for result navigation
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
    // Plan independent sections without joining their coordinate systems, then
    // share one sequential decode across all sections and height-split parts.
    static RenderResult renderSections(LongshotFrameSource& source, const QString& path, qint64 startMs, qint64 endMs,
                                       const QRect& crop, const AnalysisResult& analysis, const LongshotOptions& options,
                                       const ProgressFn& progress);
    static RenderResult render(LongshotFrameSource& source, const QString& path, qint64 startMs, qint64 endMs,
                               const QRect& crop, const AnalysisResult& analysis, const LongshotOptions& options,
                               const ProgressFn& progress, qint64 outputBudgetBytes = kOutputBudgetBytes);

    // One frame per tile. Candidates must cover its valid rows and, when
    // supplied, columns. They
    // are partitioned into agreeing groups (thumbnails within
    // kMajorityDiffThreshold and full-resolution row means within
    // kRowDeviation on all but a few rows); the group containing the LATEST
    // frame wins (later observations override earlier transients such as lazy
    // placeholders), and within it the best score: stationary (+2), keyframe
    // (+1), tile centre near frame centre (+0..1), later frames win ties.
    // No current reader sets FrameFeatures::keyFrame (IVideoFrameReader exposes
    // no keyframe flag), so the keyframe bonus is inert until one does.
    static std::vector<TileAssignment> assignTiles(const AnalysisResult& analysis, const LongshotOptions& options,
                                                   int outputHeight, int minPosition, const QRect& columns = {});

    // Moves each boundary between different frames to the lowest-gradient
    // row within kSeamSearchRows of it (gradient of the frame above), never
    // across rows on which the two frames' row means disagree.
    static void placeSeams(std::vector<TileAssignment>& tiles, const AnalysisResult& analysis, int minPosition);
};

} // namespace SnapTray::Longshot
