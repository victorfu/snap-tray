#pragma once

#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>

namespace snaptray::scrollcapture {

inline constexpr qint64 kDefaultMaxScrollContentPixels = 64LL * 1024 * 1024;

// Lossless, settled frames of the SAME platform scroll viewport. The caller
// must validate injection, geometry and platform-prior synchronization first.
struct ScrollContentPair {
    QImage before;
    QImage after;
    int shift = 0;
};

struct ScrollContentResult {
    QString error;
    // Physical pixels relative to the original viewport, unchanged for ALL pairs.
    QRect contentRect;
    QImage image;
    bool ok() const { return error.isEmpty() && !image.isNull(); }
};

// Opt-in content-only export. Finds a common rectangle with EXACT overlap in
// every pair, excluding fixed/independently moving content. It never changes
// the supplied shifts, rescales, or promotes whole-frame success. The initial
// frame prefix retains full width (including a title spanning above a sidebar);
// subsequent strips contain only the content rectangle with transparent margins.
// Downward motion only; rejects minority-width/height crops, weak detail,
// discontinuities between pairs, and outputs exceeding the allocation budget.
// Each shift must be positive and less than half the viewport height, preserving
// enough overlap for independent row evidence. Error codes: invalid_input,
// invalid_geometry, non_positive_shift, shift_too_large, pixel_budget_exceeded,
// allocation_failed, no_common_content_region, discontinuous_capture,
// insufficient_content_evidence. Failures always return a null image.
ScrollContentResult assembleScrollContent(const QVector<ScrollContentPair>& pairs,
                                         qint64 maxOutputPixels = kDefaultMaxScrollContentPixels);

} // namespace snaptray::scrollcapture
