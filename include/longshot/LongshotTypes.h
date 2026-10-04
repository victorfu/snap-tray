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
constexpr int kAnalysisVersion = 2;

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
    bool converged = true;                     // false: an island solve hit its iteration cap; positions are unusable
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
    OutOfMemory,       // the output image could not be allocated
    TooManyFrames,     // trim range exceeds PipelineParams::maxAnalyzedFrames
};

} // namespace SnapTray::Longshot
