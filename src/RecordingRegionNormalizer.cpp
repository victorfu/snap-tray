#include "RecordingRegionNormalizer.h"

#include "utils/CoordinateHelper.h"

namespace {
qreal effectiveDpr(qreal dpr)
{
    return dpr > 0.0 ? dpr : 1.0;
}

bool hasEvenPhysicalWidth(const QRect& logicalRegion, qreal dpr)
{
    const int physicalWidth = CoordinateHelper::toPhysical(logicalRegion.size(), dpr).width();
    return (physicalWidth % 2) == 0;
}

bool hasEvenPhysicalHeight(const QRect& logicalRegion, qreal dpr)
{
    const int physicalHeight = CoordinateHelper::toPhysical(logicalRegion.size(), dpr).height();
    return (physicalHeight % 2) == 0;
}

void normalizeWidth(QRect& logicalRegion, const QRect& logicalBounds, qreal dpr)
{
    if (hasEvenPhysicalWidth(logicalRegion, dpr)) {
        return;
    }

    const int originalRight = logicalRegion.right();
    const int maxExpandRight = logicalBounds.right() - originalRight;
    for (int delta = 1; delta <= maxExpandRight; ++delta) {
        QRect candidate = logicalRegion;
        candidate.setRight(originalRight + delta);
        if (hasEvenPhysicalWidth(candidate, dpr)) {
            logicalRegion = candidate;
            return;
        }
    }

    const int originalLeft = logicalRegion.left();
    const int maxExpandLeft = originalLeft - logicalBounds.left();
    for (int delta = 1; delta <= maxExpandLeft; ++delta) {
        QRect candidate = logicalRegion;
        candidate.setLeft(originalLeft - delta);
        if (hasEvenPhysicalWidth(candidate, dpr)) {
            logicalRegion = candidate;
            return;
        }
    }

    // Try expanding both sides before cropping.
    const int maxTotalExpand = maxExpandLeft + maxExpandRight;
    for (int totalDelta = 2; totalDelta <= maxTotalExpand; ++totalDelta) {
        const int minRightDelta = qMax(1, totalDelta - maxExpandLeft);
        const int maxRightDelta = qMin(maxExpandRight, totalDelta - 1);
        for (int rightDelta = maxRightDelta; rightDelta >= minRightDelta; --rightDelta) {
            const int leftDelta = totalDelta - rightDelta;
            QRect candidate = logicalRegion;
            candidate.setLeft(originalLeft - leftDelta);
            candidate.setRight(originalRight + rightDelta);
            if (hasEvenPhysicalWidth(candidate, dpr)) {
                logicalRegion = candidate;
                return;
            }
        }
    }

    for (int delta = 1; delta < logicalRegion.width(); ++delta) {
        QRect candidate = logicalRegion;
        candidate.setRight(originalRight - delta);
        if (hasEvenPhysicalWidth(candidate, dpr)) {
            logicalRegion = candidate;
            return;
        }
    }
}

void normalizeHeight(QRect& logicalRegion, const QRect& logicalBounds, qreal dpr)
{
    if (hasEvenPhysicalHeight(logicalRegion, dpr)) {
        return;
    }

    const int originalBottom = logicalRegion.bottom();
    const int maxExpandBottom = logicalBounds.bottom() - originalBottom;
    for (int delta = 1; delta <= maxExpandBottom; ++delta) {
        QRect candidate = logicalRegion;
        candidate.setBottom(originalBottom + delta);
        if (hasEvenPhysicalHeight(candidate, dpr)) {
            logicalRegion = candidate;
            return;
        }
    }

    const int originalTop = logicalRegion.top();
    const int maxExpandTop = originalTop - logicalBounds.top();
    for (int delta = 1; delta <= maxExpandTop; ++delta) {
        QRect candidate = logicalRegion;
        candidate.setTop(originalTop - delta);
        if (hasEvenPhysicalHeight(candidate, dpr)) {
            logicalRegion = candidate;
            return;
        }
    }

    // Try expanding both sides before cropping.
    const int maxTotalExpand = maxExpandTop + maxExpandBottom;
    for (int totalDelta = 2; totalDelta <= maxTotalExpand; ++totalDelta) {
        const int minBottomDelta = qMax(1, totalDelta - maxExpandTop);
        const int maxBottomDelta = qMin(maxExpandBottom, totalDelta - 1);
        for (int bottomDelta = maxBottomDelta; bottomDelta >= minBottomDelta; --bottomDelta) {
            const int topDelta = totalDelta - bottomDelta;
            QRect candidate = logicalRegion;
            candidate.setTop(originalTop - topDelta);
            candidate.setBottom(originalBottom + bottomDelta);
            if (hasEvenPhysicalHeight(candidate, dpr)) {
                logicalRegion = candidate;
                return;
            }
        }
    }

    for (int delta = 1; delta < logicalRegion.height(); ++delta) {
        QRect candidate = logicalRegion;
        candidate.setBottom(originalBottom - delta);
        if (hasEvenPhysicalHeight(candidate, dpr)) {
            logicalRegion = candidate;
            return;
        }
    }
}
} // namespace

QRect normalizeToEvenPhysicalRegion(const QRect& logicalRegion,
                                    const QRect& logicalScreenBounds,
                                    qreal dpr)
{
    const QRect normalizedBounds = logicalScreenBounds.normalized();
    QRect normalizedRegion = logicalRegion.normalized().intersected(normalizedBounds);
    if (normalizedRegion.isEmpty()) {
        return normalizedRegion;
    }

    const qreal normalizedDpr = effectiveDpr(dpr);

    normalizeWidth(normalizedRegion, normalizedBounds, normalizedDpr);
    normalizeHeight(normalizedRegion, normalizedBounds, normalizedDpr);

    return normalizedRegion.intersected(normalizedBounds);
}
