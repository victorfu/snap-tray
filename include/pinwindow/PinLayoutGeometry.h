#pragma once

#include <QRect>
#include <QSize>
#include <QVector>

namespace PinLayoutGeometry {
enum class Mode { Original, Horizontal, Vertical };
inline constexpr int MaxCanvasExtent = 10000;
inline constexpr int MaxSources = 20;
// Bound the output allocation as well as logical extents (DPR may exceed 1).
inline constexpr qint64 MaxOutputPixels = 64LL * 1000 * 1000;

struct Source {
    QSize pixels;
    qreal dpr = 1.0;
    // Optional natural logical size, independent of the current placement.
    // PNG round trips need not preserve QImage's transient DPR.
    QSize logicalSize;
};
struct Options {
    Mode mode = Mode::Original;
    int gap = 8; // logical output units, independent of display zoom
    bool preventUpscale = true;
    int maxRowWidth = 0; // wrapping is only used by Original
    int rowGap = -1;
    QVector<int> order; // permutation of source indices; empty means input order
};
struct Result {
    QVector<QRect> placements; // logical bounds, indexed by source, not order
    QSize logicalSize;
    QSize physicalSize;
    bool valid() const { return !placements.isEmpty(); }
};

bool safeOutputSize(const QSize& logicalSize, qreal outputDpr);
Result calculate(const QVector<Source>& sources, qreal outputDpr, const Options& options);
} // namespace PinLayoutGeometry
