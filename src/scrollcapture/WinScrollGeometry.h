#pragma once

#include <QRectF>

namespace snaptray::scrollcapture {

inline QRect logicalScrollBounds(const QRectF& physicalBounds, const QPoint& screenOrigin,
                                 qreal devicePixelRatio)
{
    const QPointF origin(screenOrigin);
    const QPointF topLeft = origin + (physicalBounds.topLeft() - origin) / devicePixelRatio;
    const QPointF bottomRight = origin + (physicalBounds.bottomRight() - origin) / devicePixelRatio;
    return QRectF(topLeft, bottomRight).toAlignedRect();
}

} // namespace snaptray::scrollcapture
