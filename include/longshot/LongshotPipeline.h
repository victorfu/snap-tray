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
    int maxAnalyzedFrames = 4000;      // ≈ 100 MB of row features and thumbnails; longer ranges must be trimmed
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
    int pixelRejectedClosures = 0;     // closures the analyzer accepted but the pixel check refused
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
