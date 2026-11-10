#pragma once

#include <QImage>
#include <QRect>
#include <QString>
#include <QVector>

namespace snaptray::scrollcapture {

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
    QRect viewport;
    QImage image;
};

// Opt-in content-only export. Finds a common rectangle with EXACT overlap in
// every pair, excluding fixed/independently moving content. It never changes
// the supplied shifts, rescales, or promotes whole-frame success. The initial
// frame prefix retains full width (including a title spanning above a sidebar);
// subsequent strips contain only the content rectangle with transparent margins.
// Downward motion only; rejects minority-width/height crops, weak detail,
// discontinuities between pairs, and outputs exceeding the allocation budget.
ScrollContentResult assembleScrollContent(const QVector<ScrollContentPair>& pairs,
                                         qint64 maxOutputPixels = 64 * 1024 * 1024);

} // namespace snaptray::scrollcapture
