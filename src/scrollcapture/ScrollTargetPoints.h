#pragma once

#include <QList>
#include <QPoint>
#include <QRect>

namespace snaptray::scrollcapture {

// Points inside a scroll area to try as the wheel target, centre first. A
// nested scroll area or an overlapping window can cover the centre, so
// injectors hit-test each point and use the first that reaches the selected
// area itself.
inline QList<QPoint> scrollTargetPoints(const QRect& bounds)
{
    constexpr int kInset = 8;
    static constexpr double kFractions[][2] = {
        {0.5, 0.5}, {0.5, 0.25}, {0.5, 0.75}, {0.25, 0.5}, {0.75, 0.5},
        {0.25, 0.25}, {0.75, 0.25}, {0.25, 0.75}, {0.75, 0.75}, {0.5, 0.1}, {0.5, 0.9},
    };
    QList<QPoint> points;
    const QRect inner = bounds.adjusted(kInset, kInset, -kInset, -kInset);
    if (inner.isEmpty()) {
        points.append(bounds.center());
        return points;
    }
    for (const auto& fraction : kFractions) {
        const QPoint point(inner.left() + qRound(fraction[0] * (inner.width() - 1)),
                           inner.top() + qRound(fraction[1] * (inner.height() - 1)));
        if (!points.contains(point)) {
            points.append(point);
        }
    }
    return points;
}

} // namespace snaptray::scrollcapture
