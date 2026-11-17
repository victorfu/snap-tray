#include "pinwindow/PinLayoutGeometry.h"

#include <algorithm>
#include <cmath>
#include <numeric>
#include <QtMath>

namespace PinLayoutGeometry {
bool safeOutputSize(const QSize& size, qreal dpr)
{
    if (!std::isfinite(dpr) || dpr <= 0 || size.isEmpty()
        || size.width() > MaxCanvasExtent || size.height() > MaxCanvasExtent)
        return false;
    const double width = std::ceil(size.width() * dpr);
    const double height = std::ceil(size.height() * dpr);
    return std::round(size.width() * dpr) >= 1 && std::round(size.height() * dpr) >= 1
        && width <= MaxOutputPixels
        && height <= MaxOutputPixels && width * height <= MaxOutputPixels;
}

Result calculate(const QVector<Source>& sources, qreal outputDpr, const Options& options)
{
    if (sources.isEmpty() || sources.size() > MaxSources
        || !std::isfinite(outputDpr) || outputDpr <= 0 || options.gap < 0
        || options.gap > MaxCanvasExtent || options.rowGap > MaxCanvasExtent
        || (options.mode != Mode::Original && options.mode != Mode::Horizontal
            && options.mode != Mode::Vertical))
        return {};

    QVector<int> order = options.order;
    if (order.isEmpty()) {
        order.resize(sources.size());
        std::iota(order.begin(), order.end(), 0);
    }
    auto sorted = order;
    std::sort(sorted.begin(), sorted.end());
    if (sorted.size() != sources.size()) return {};
    for (int i = 0; i < sorted.size(); ++i)
        if (sorted[i] != i) return {};

    const bool horizontal = options.mode == Mode::Horizontal;
    double common = options.preventUpscale ? INFINITY : 0.0;
    for (const auto& source : sources) {
        if (source.pixels.isEmpty() || !std::isfinite(source.dpr) || source.dpr <= 0)
            return {};
        const int dimension = horizontal ? source.pixels.height() : source.pixels.width();
        const double naturalDimension = source.logicalSize.isEmpty()
            ? dimension / source.dpr
            : horizontal ? source.logicalSize.height() : source.logicalSize.width();
        // No-upscale compares physical source pixels to physical output pixels.
        // Otherwise use the largest natural logical dimension as the target.
        common = options.preventUpscale
            ? std::min(common, dimension / outputDpr)
            : std::max(common, naturalDimension);
    }
    if (options.mode != Mode::Original) {
        common = options.preventUpscale ? std::floor(common) : std::round(common);
        if (common < 1 || common > MaxCanvasExtent) return {};
    }

    Result result;
    result.placements.resize(sources.size());
    qint64 x = 0, y = 0, rowHeight = 0, width = 0, height = 0;
    const int rowGap = options.rowGap < 0 ? options.gap : options.rowGap;
    for (int index : order) {
        const auto& source = sources[index];
        double w = source.logicalSize.isEmpty() ? source.pixels.width() / source.dpr : source.logicalSize.width();
        double h = source.logicalSize.isEmpty() ? source.pixels.height() / source.dpr : source.logicalSize.height();
        if (options.mode == Mode::Original) {
            const double scale = options.preventUpscale
                ? std::min({1.0, source.pixels.width() / (w * outputDpr),
                            source.pixels.height() / (h * outputDpr)}) : 1.0;
            w *= scale;
            h *= scale;
        } else if (horizontal) {
            h = common;
            w = common * source.pixels.width() / source.pixels.height();
        } else {
            w = common;
            h = common * source.pixels.height() / source.pixels.width();
        }
        if (!std::isfinite(w) || !std::isfinite(h) || w < 0.5 || h < 0.5
            || w > MaxCanvasExtent || h > MaxCanvasExtent)
            return {};
        const QSize size(
            options.preventUpscale ? qMin(qRound(w), qFloor(std::min<double>(MaxCanvasExtent,
                source.pixels.width() / outputDpr))) : qRound(w),
            options.preventUpscale ? qMin(qRound(h), qFloor(std::min<double>(MaxCanvasExtent,
                source.pixels.height() / outputDpr))) : qRound(h));
        if (size.isEmpty()) return {};
        if (options.preventUpscale
            && (qCeil(size.width() * outputDpr) > source.pixels.width()
                || qCeil(size.height() * outputDpr) > source.pixels.height()))
            return {}; // do not silently round a tiny source up beyond its pixels
        if (options.mode == Mode::Original && options.maxRowWidth > 0
            && x > 0 && x + size.width() > options.maxRowWidth) {
            x = 0;
            y += rowHeight + rowGap;
            rowHeight = 0;
        }
        if (x + size.width() > MaxCanvasExtent || y + size.height() > MaxCanvasExtent)
            return {};
        result.placements[index] = QRect(QPoint(int(x), int(y)), size);
        width = std::max(width, x + size.width());
        height = std::max(height, y + size.height());
        rowHeight = std::max(rowHeight, qint64(size.height()));
        if (options.mode == Mode::Vertical) y += size.height() + qint64(options.gap);
        else x += size.width() + qint64(options.gap);
    }
    result.logicalSize = QSize(int(width), int(height));
    if (!safeOutputSize(result.logicalSize, outputDpr)) return {};
    result.physicalSize = QSize(qRound(width * outputDpr), qRound(height * outputDpr));
    return result;
}
} // namespace PinLayoutGeometry
