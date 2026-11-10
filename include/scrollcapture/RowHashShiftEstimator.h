#pragma once

#include <QVector>
#include <QImage>
#include <QtGlobal>

#include <optional>

namespace snaptray::scrollcapture {

// Measures how far the content of a scroll area moved between two lossless
// frames, from their per-row hashes (computeRowHashes).
//
// A shift s means the content moved up by s rows, i.e. the view scrolled
// toward the bottom: after[i] == before[i + s]. Negative shifts mean the view
// scrolled toward the top.
//
// Rows that are identical at the same position in both frames (sticky headers,
// toolbars, blank margins) are counted as static and do not vote. Only rows
// whose content is unique within `before` vote, so blank or repeating content
// cannot pick a shift on its own. When the evidence is weak or two shifts are
// almost equally supported, the estimate says so instead of guessing.
struct ShiftEstimatorOptions {
    // Shift the platform reports (in device pixels). When set, only shifts
    // within `searchRadius` of it are considered.
    std::optional<int> expectedShift;
    int searchRadius = 64;
    // A shift must leave at least this many rows overlapping.
    int minOverlapRows = 16;
    // Unique rows that must agree on the winning shift.
    int minDistinctMatches = 8;
    // Of the moving rows that overlap at the winning shift, the fraction that
    // must match exactly.
    double minMatchRatio = 0.5;
    // A runner-up with at least this fraction of the winner's votes makes the
    // result ambiguous.
    double ambiguityRatio = 0.9;
};

struct ShiftEstimate {
    enum class Status {
        // `shift` is reliable (0 when nothing moved).
        Found,
        // Two or more shifts are supported almost equally; see `candidates`.
        Ambiguous,
        // Independently measured regions have different displacements. There
        // is no single alignment for this frame, even with a dominant shift.
        NonUniformMotion,
        // Too few unique rows agree (blank or repeating content).
        InsufficientDetail,
        // The moving rows do not match at any allowed shift.
        NotFound,
        // Frames are empty or have different heights.
        InvalidInput,
    };

    Status status = Status::InvalidInput;
    int shift = 0;
    // Votes from unique rows for `shift`.
    int distinctMatches = 0;
    // Moving rows that match exactly at `shift`, out of `overlapRows`.
    int matchedRows = 0;
    int overlapRows = 0;
    // Rows identical at the same position in both frames.
    int staticRows = 0;
    // For Ambiguous: every shift close to the best, best first.
    // For NonUniformMotion: independently observed displacements, sorted.
    QVector<int> candidates;
    // Shifts that were searched (inclusive); both 0 when no search ran.
    int searchLow = 0;
    int searchHigh = 0;
    // The best-supported shift other than `shift`, and its votes, so a log
    // can show how close the second choice was.
    std::optional<int> runnerUpShift;
    int runnerUpVotes = 0;
};

ShiftEstimate estimateVerticalShift(const QVector<quint64>& before,
                                    const QVector<quint64>& after,
                                    const ShiftEstimatorOptions& options = {});

const char* shiftEstimateStatusName(ShiftEstimate::Status status);

struct BandShiftEstimate {
    ShiftEstimate estimate;
    // Conservative alignment result: rejects conflicting or unresolved moving
    // bands even when a dominant displacement was measured successfully.
    ShiftEstimate alignmentEstimate;
    QVector<ShiftEstimate> bands;
    // Unconstrained observations used to detect conflicts outside the prior.
    QVector<ShiftEstimate> observedBands;
    QVector<int> conflictingBands;
    QVector<int> unresolvedBands;
    int supportingBands = 0;
    // A dominant displacement is not a promise that the entire frame can be
    // stitched: sticky/independently scrolling columns may disagree.
    bool nonUniformMotion = false;
};

// Eight equal-width vertical bands, each using the same exact-row criteria.
// A displacement needs a strict majority of ALL bands, including blank ones.
// No band is selected using the expected shift and no thresholds are relaxed.
BandShiftEstimate estimateVerticalShiftByBands(const QImage& before, const QImage& after,
                                              const ShiftEstimatorOptions& options = {},
                                              int horizontalInset = 0);

} // namespace snaptray::scrollcapture
