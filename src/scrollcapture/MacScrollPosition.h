#pragma once

#include "scrollcapture/ScrollAreaLocator.h"

#include <QRectF>
#include <algorithm>

namespace snaptray::scrollcapture {

// AXWebArea frames describe a web viewport, not NSScrollView document geometry.
// Keep missing web positions unknown; real scroll-bar values remain usable.
// Pure geometry policy so platform AX observations can be regression-tested.
inline void resolveMacScrollPosition(ScrollPosition& position, const QRectF& viewport,
                                     const QString& contentsRole,
                                     const std::optional<QRectF>& contentsFrame)
{
    constexpr double kEdgeFractionEpsilon = 0.001;
    constexpr double kEdgePixelEpsilon = 0.5;
    const double viewportHeight = viewport.height();
    if (contentsFrame && contentsRole != QStringLiteral("AXWebArea") &&
        contentsFrame->height() + kEdgePixelEpsilon >= viewportHeight) {
        position.contentHeight = contentsFrame->height();
        position.offset = viewport.y() - contentsFrame->y();
        position.sources << QStringLiteral("AXContents");
    }

    if (!position.contentHeight && position.viewportRatio && *position.viewportRatio > 0.0) {
        position.contentHeight = viewportHeight / *position.viewportRatio;
    }
    if (position.contentHeight && !position.viewportRatio && *position.contentHeight > 0.0) {
        position.viewportRatio = std::min(1.0, viewportHeight / *position.contentHeight);
    }
    const std::optional<double> scrollRange = position.contentHeight
        ? std::optional<double>(std::max(0.0, *position.contentHeight - viewportHeight))
        : std::nullopt;
    if (!position.fraction && position.offset && scrollRange && *scrollRange > 0.0) {
        position.fraction = std::clamp(*position.offset / *scrollRange, 0.0, 1.0);
    }
    if (!position.offset && position.fraction && scrollRange) {
        position.offset = *position.fraction * *scrollRange;
    }

    if (scrollRange && *scrollRange <= kEdgePixelEpsilon) {
        position.atTop = true;
        position.atBottom = true;
    } else if (position.fraction) {
        position.atTop = *position.fraction <= kEdgeFractionEpsilon;
        position.atBottom = *position.fraction >= 1.0 - kEdgeFractionEpsilon;
    } else if (position.offset && scrollRange) {
        position.atTop = *position.offset <= kEdgePixelEpsilon;
        position.atBottom = *position.offset >= *scrollRange - kEdgePixelEpsilon;
    }
}

} // namespace snaptray::scrollcapture
