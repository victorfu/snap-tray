#pragma once

#include "longshot/LongshotTypes.h"

#include <QImage>

#include <optional>

namespace SnapTray::Longshot {

struct AnalyzerParams {
    double minPeakScore = 0.55;          // NCC below this is not a match
    double minMargin = 0.15;             // best - runner-up below this is ambiguous (periodic content)
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
    // nullopt when no candidate reaches minPeakScore / minMargin.
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
